#include "tiny_renderer/gltf_loader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "tiny_renderer/affine_timeline.hpp"
#include "tiny_renderer/quaternion.hpp"
#include "tiny_renderer/skeletal_trs_timeline.hpp"

namespace tiny_renderer {
namespace {

constexpr std::size_t kMaxGltfJsonBytes = 1024U * 1024U;
constexpr std::size_t kMaxGltfBinaryBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaxGltfBufferViews = 128U;
constexpr std::size_t kMaxGltfAccessors = 128U;
constexpr std::size_t kMaxGltfNodes = 1024U;
constexpr std::size_t kMaxGltfAnimations = 16U;
constexpr std::size_t kMaxGltfVertices = 262144U;
constexpr std::size_t kMaxGltfIndices = 786432U;
constexpr std::size_t kMaxJsonDepth = 64U;

[[noreturn]] void fail(const std::string& message) {
    throw GltfLoadError("glTF: " + message);
}

void validate_gltf_affine_matrix(
    const Mat4& matrix,
    std::string_view label) {
    try {
        detail::validate_bounded_affine_matrix(matrix, label);
    } catch (const std::invalid_argument& error) {
        fail(error.what());
    }
}

[[nodiscard]] bool checked_add(
    std::size_t left,
    std::size_t right,
    std::size_t& result) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_mul(
    std::size_t left,
    std::size_t right,
    std::size_t& result) {
    if (left != 0U
        && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] std::string read_text_file(
    const std::filesystem::path& path,
    std::size_t limit) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        fail("failed to open '" + path.string() + "'");
    }
    const std::streamoff end = input.tellg();
    if (end < 0) {
        fail("failed to determine size of '" + path.string() + "'");
    }
    const auto size64 = static_cast<std::uintmax_t>(end);
    if (size64 > static_cast<std::uintmax_t>(limit)) {
        fail("JSON file exceeds bounded size limit");
    }
    const std::size_t size = static_cast<std::size_t>(size64);
    std::string text(size, '\0');
    input.seekg(0, std::ios::beg);
    if (size != 0U
        && !input.read(text.data(), static_cast<std::streamsize>(size))) {
        fail("failed to read complete JSON file");
    }
    return text;
}

[[nodiscard]] std::vector<std::uint8_t> read_binary_file(
    const std::filesystem::path& path,
    std::size_t limit) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        fail("failed to open external buffer '" + path.string() + "'");
    }
    const std::streamoff end = input.tellg();
    if (end < 0) {
        fail("failed to determine external buffer size");
    }
    const auto size64 = static_cast<std::uintmax_t>(end);
    if (size64 > static_cast<std::uintmax_t>(limit)) {
        fail("external buffer exceeds bounded size limit");
    }
    const std::size_t size = static_cast<std::size_t>(size64);
    std::vector<std::uint8_t> bytes(size);
    input.seekg(0, std::ios::beg);
    if (size != 0U
        && !input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size))) {
        fail("failed to read complete external buffer");
    }
    return bytes;
}

struct JsonValue {
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object,
    };

    Type type{Type::Null};
    bool boolean{};
    double number{};
    std::string string{};
    std::vector<JsonValue> array{};
    std::map<std::string, JsonValue> object{};
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    [[nodiscard]] JsonValue parse() {
        skip_space();
        JsonValue value = parse_value(0U);
        skip_space();
        if (position_ != text_.size()) {
            fail("unexpected trailing JSON content");
        }
        return value;
    }

private:
    [[nodiscard]] JsonValue parse_value(std::size_t depth) {
        if (depth > kMaxJsonDepth) {
            fail("JSON nesting exceeds bounded depth");
        }
        skip_space();
        if (position_ >= text_.size()) {
            fail("unexpected end of JSON");
        }
        const char c = text_[position_];
        if (c == '{') {
            return parse_object(depth + 1U);
        }
        if (c == '[') {
            return parse_array(depth + 1U);
        }
        if (c == '"') {
            JsonValue value;
            value.type = JsonValue::Type::String;
            value.string = parse_string();
            return value;
        }
        if (c == 't') {
            consume_literal("true");
            JsonValue value;
            value.type = JsonValue::Type::Boolean;
            value.boolean = true;
            return value;
        }
        if (c == 'f') {
            consume_literal("false");
            JsonValue value;
            value.type = JsonValue::Type::Boolean;
            return value;
        }
        if (c == 'n') {
            consume_literal("null");
            return {};
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            JsonValue value;
            value.type = JsonValue::Type::Number;
            value.number = parse_number();
            return value;
        }
        fail("unexpected JSON token");
    }

    [[nodiscard]] JsonValue parse_object(std::size_t depth) {
        expect('{');
        JsonValue value;
        value.type = JsonValue::Type::Object;
        skip_space();
        if (peek('}')) {
            ++position_;
            return value;
        }
        while (true) {
            skip_space();
            if (!peek('"')) {
                fail("JSON object key must be a string");
            }
            const std::string key = parse_string();
            skip_space();
            expect(':');
            JsonValue child = parse_value(depth);
            const auto [iterator, inserted] =
                value.object.emplace(key, std::move(child));
            (void)iterator;
            if (!inserted) {
                fail("duplicate JSON object key '" + key + "'");
            }
            skip_space();
            if (peek('}')) {
                ++position_;
                break;
            }
            expect(',');
        }
        return value;
    }

    [[nodiscard]] JsonValue parse_array(std::size_t depth) {
        expect('[');
        JsonValue value;
        value.type = JsonValue::Type::Array;
        skip_space();
        if (peek(']')) {
            ++position_;
            return value;
        }
        while (true) {
            value.array.push_back(parse_value(depth));
            skip_space();
            if (peek(']')) {
                ++position_;
                break;
            }
            expect(',');
        }
        return value;
    }

    static void append_utf8(std::string& output, std::uint32_t codepoint) {
        if (codepoint <= 0x7FU) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FFU) {
            output.push_back(
                static_cast<char>(0xC0U | (codepoint >> 6U)));
            output.push_back(
                static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else if (codepoint <= 0xFFFFU) {
            output.push_back(
                static_cast<char>(0xE0U | (codepoint >> 12U)));
            output.push_back(
                static_cast<char>(
                    0x80U | ((codepoint >> 6U) & 0x3FU)));
            output.push_back(
                static_cast<char>(0x80U | (codepoint & 0x3FU)));
        } else {
            output.push_back(
                static_cast<char>(0xF0U | (codepoint >> 18U)));
            output.push_back(
                static_cast<char>(
                    0x80U | ((codepoint >> 12U) & 0x3FU)));
            output.push_back(
                static_cast<char>(
                    0x80U | ((codepoint >> 6U) & 0x3FU)));
            output.push_back(
                static_cast<char>(0x80U | (codepoint & 0x3FU)));
        }
    }

    [[nodiscard]] std::uint32_t parse_hex4() {
        if (text_.size() - position_ < 4U) {
            fail("truncated JSON unicode escape");
        }
        std::uint32_t value = 0U;
        for (std::size_t i = 0U; i < 4U; ++i) {
            const char c = text_[position_++];
            value <<= 4U;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                fail("invalid JSON unicode escape");
            }
        }
        return value;
    }

    [[nodiscard]] std::string parse_string() {
        expect('"');
        std::string result;
        while (position_ < text_.size()) {
            const unsigned char byte =
                static_cast<unsigned char>(text_[position_++]);
            if (byte == static_cast<unsigned char>('"')) {
                return result;
            }
            if (byte < 0x20U) {
                fail("JSON string contains an unescaped control byte");
            }
            if (byte != static_cast<unsigned char>('\\')) {
                result.push_back(static_cast<char>(byte));
                continue;
            }
            if (position_ >= text_.size()) {
                fail("truncated JSON string escape");
            }
            const char escape = text_[position_++];
            switch (escape) {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escape);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u': {
                    std::uint32_t codepoint = parse_hex4();
                    if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
                        if (text_.size() - position_ < 6U
                            || text_[position_] != '\\'
                            || text_[position_ + 1U] != 'u') {
                            fail("JSON high surrogate lacks low surrogate");
                        }
                        position_ += 2U;
                        const std::uint32_t low = parse_hex4();
                        if (low < 0xDC00U || low > 0xDFFFU) {
                            fail("invalid JSON low surrogate");
                        }
                        codepoint = 0x10000U
                            + ((codepoint - 0xD800U) << 10U)
                            + (low - 0xDC00U);
                    } else if (
                        codepoint >= 0xDC00U
                        && codepoint <= 0xDFFFU) {
                        fail("unexpected JSON low surrogate");
                    }
                    append_utf8(result, codepoint);
                    break;
                }
                default:
                    fail("invalid JSON string escape");
            }
        }
        fail("unterminated JSON string");
    }

    [[nodiscard]] double parse_number() {
        const std::size_t begin = position_;
        if (peek('-')) {
            ++position_;
        }
        if (position_ >= text_.size()) {
            fail("truncated JSON number");
        }
        if (text_[position_] == '0') {
            ++position_;
            if (position_ < text_.size()
                && text_[position_] >= '0'
                && text_[position_] <= '9') {
                fail("JSON number has a leading zero");
            }
        } else {
            if (text_[position_] < '1' || text_[position_] > '9') {
                fail("invalid JSON number");
            }
            while (position_ < text_.size()
                   && text_[position_] >= '0'
                   && text_[position_] <= '9') {
                ++position_;
            }
        }
        if (peek('.')) {
            ++position_;
            const std::size_t fractional_begin = position_;
            while (position_ < text_.size()
                   && text_[position_] >= '0'
                   && text_[position_] <= '9') {
                ++position_;
            }
            if (position_ == fractional_begin) {
                fail("JSON fraction requires digits");
            }
        }
        if (peek('e') || peek('E')) {
            ++position_;
            if (peek('+') || peek('-')) {
                ++position_;
            }
            const std::size_t exponent_begin = position_;
            while (position_ < text_.size()
                   && text_[position_] >= '0'
                   && text_[position_] <= '9') {
                ++position_;
            }
            if (position_ == exponent_begin) {
                fail("JSON exponent requires digits");
            }
        }

        const char* first = text_.data() + begin;
        const char* last = text_.data() + position_;
        double value = 0.0;
        const auto [pointer, error] =
            std::from_chars(first, last, value, std::chars_format::general);
        if (error != std::errc{} || pointer != last
            || !std::isfinite(value)) {
            fail("JSON number is not finite or representable");
        }
        return value;
    }

    void consume_literal(std::string_view literal) {
        if (text_.substr(position_, literal.size()) != literal) {
            fail("invalid JSON literal");
        }
        position_ += literal.size();
    }

    void skip_space() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
                break;
            }
            ++position_;
        }
    }

    [[nodiscard]] bool peek(char c) const {
        return position_ < text_.size() && text_[position_] == c;
    }

    void expect(char c) {
        skip_space();
        if (!peek(c)) {
            fail(std::string("expected JSON '") + c + "'");
        }
        ++position_;
    }

    std::string_view text_;
    std::size_t position_{};
};

[[nodiscard]] const std::map<std::string, JsonValue>& as_object(
    const JsonValue& value,
    std::string_view label) {
    if (value.type != JsonValue::Type::Object) {
        fail(std::string(label) + " must be an object");
    }
    return value.object;
}

[[nodiscard]] const std::vector<JsonValue>& as_array(
    const JsonValue& value,
    std::string_view label) {
    if (value.type != JsonValue::Type::Array) {
        fail(std::string(label) + " must be an array");
    }
    return value.array;
}

[[nodiscard]] const std::string& as_string(
    const JsonValue& value,
    std::string_view label) {
    if (value.type != JsonValue::Type::String) {
        fail(std::string(label) + " must be a string");
    }
    return value.string;
}

[[nodiscard]] const JsonValue* optional_member(
    const std::map<std::string, JsonValue>& object,
    std::string_view key) {
    const auto iterator = object.find(std::string(key));
    return iterator == object.end() ? nullptr : &iterator->second;
}

[[nodiscard]] const JsonValue& require_member(
    const std::map<std::string, JsonValue>& object,
    std::string_view key,
    std::string_view label) {
    const JsonValue* value = optional_member(object, key);
    if (!value) {
        fail(std::string(label) + " requires '" + std::string(key) + "'");
    }
    return *value;
}

[[nodiscard]] std::size_t as_index(
    const JsonValue& value,
    std::string_view label) {
    if (value.type != JsonValue::Type::Number
        || value.number < 0.0
        || value.number
            > static_cast<double>(
                std::numeric_limits<std::size_t>::max())
        || std::floor(value.number) != value.number) {
        fail(std::string(label) + " must be a non-negative integer");
    }
    return static_cast<std::size_t>(value.number);
}

[[nodiscard]] int as_int(
    const JsonValue& value,
    std::string_view label) {
    const std::size_t raw = as_index(value, label);
    if (raw > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        fail(std::string(label) + " exceeds integer range");
    }
    return static_cast<int>(raw);
}

[[nodiscard]] bool as_bool(
    const JsonValue& value,
    std::string_view label) {
    if (value.type != JsonValue::Type::Boolean) {
        fail(std::string(label) + " must be a boolean");
    }
    return value.boolean;
}

[[nodiscard]] float as_float(
    const JsonValue& value,
    std::string_view label) {
    if (value.type != JsonValue::Type::Number) {
        fail(std::string(label) + " must be a finite number");
    }
    const float result = static_cast<float>(value.number);
    if (!std::isfinite(result)) {
        fail(std::string(label) + " is not representable as float");
    }
    return result;
}

void reject_member(
    const std::map<std::string, JsonValue>& object,
    std::string_view key,
    std::string_view label) {
    if (optional_member(object, key)) {
        fail(std::string(label) + " uses unsupported '" + std::string(key) + "'");
    }
}

[[nodiscard]] std::vector<float> number_array(
    const JsonValue& value,
    std::size_t expected,
    std::string_view label) {
    const auto& array = as_array(value, label);
    if (array.size() != expected) {
        fail(std::string(label) + " has unexpected element count");
    }
    std::vector<float> result;
    result.reserve(expected);
    for (const JsonValue& element : array) {
        result.push_back(as_float(element, label));
    }
    return result;
}

struct ParsedNodeTransform {
    Mat4 local{Mat4::identity()};
    std::optional<SkeletalTrs> semantic_trs{};
};

[[nodiscard]] ParsedNodeTransform parse_node_transform(
    const std::map<std::string, JsonValue>& object) {
    const JsonValue* matrix_value = optional_member(object, "matrix");
    const bool has_trs =
        optional_member(object, "translation")
        || optional_member(object, "rotation")
        || optional_member(object, "scale");
    if (matrix_value && has_trs) {
        fail("node cannot contain both matrix and TRS state");
    }

    if (matrix_value) {
        const std::vector<float> values =
            number_array(*matrix_value, 16U, "node matrix");
        Mat4 matrix{};
        for (std::size_t column = 0U; column < 4U; ++column) {
            for (std::size_t row = 0U; row < 4U; ++row) {
                matrix(row, column) = values[column * 4U + row];
            }
        }
        validate_gltf_affine_matrix(
            matrix,
            "glTF node matrix");
        return {matrix, std::nullopt};
    }

    SkeletalTrs semantic;
    if (const JsonValue* value =
            optional_member(object, "translation")) {
        const auto values = number_array(
            *value, 3U, "node translation");
        semantic.translation = {
            values[0],
            values[1],
            values[2],
        };
    }
    if (const JsonValue* value = optional_member(object, "scale")) {
        const auto values = number_array(*value, 3U, "node scale");
        semantic.scale = {
            values[0],
            values[1],
            values[2],
        };
    }
    if (const JsonValue* value =
            optional_member(object, "rotation")) {
        const auto values = number_array(
            *value, 4U, "node rotation");
        semantic.rotation = {
            values[0],
            values[1],
            values[2],
            values[3],
        };
    }

    Mat4 local;
    try {
        local = detail::compose_skeletal_trs(
            semantic,
            "glTF node semantic TRS");
    } catch (const std::invalid_argument& error) {
        fail(error.what());
    }
    validate_gltf_affine_matrix(
        local,
        "glTF node local transform");
    return {local, semantic};
}

struct BufferViewInfo {
    std::size_t byte_offset{};
    std::size_t byte_length{};
    std::optional<std::size_t> byte_stride{};
};

struct AccessorInfo {
    std::size_t buffer_view{};
    std::size_t byte_offset{};
    int component_type{};
    std::size_t count{};
    std::string type{};
    bool normalized{};
    std::optional<std::vector<float>> min_values{};
    std::optional<std::vector<float>> max_values{};
};

struct NodeInfo {
    Mat4 local{Mat4::identity()};
    std::optional<SkeletalTrs> semantic_trs{};
    std::vector<std::size_t> children{};
    std::optional<std::size_t> mesh{};
    std::optional<std::size_t> skin{};
};

[[nodiscard]] std::size_t component_size(int component_type) {
    switch (component_type) {
        case 5121:
            return 1U;
        case 5123:
            return 2U;
        case 5125:
        case 5126:
            return 4U;
        default:
            fail("unsupported accessor componentType");
    }
}

[[nodiscard]] std::size_t component_count(
    std::string_view type) {
    if (type == "SCALAR") {
        return 1U;
    }
    if (type == "VEC3") {
        return 3U;
    }
    if (type == "VEC4") {
        return 4U;
    }
    if (type == "MAT4") {
        return 16U;
    }
    fail("unsupported accessor type '" + std::string(type) + "'");
}

struct AccessorWindow {
    const AccessorInfo* accessor{};
    const BufferViewInfo* view{};
    std::size_t element_size{};
    std::size_t stride{};
    std::size_t absolute_start{};
};

[[nodiscard]] AccessorWindow accessor_window(
    const std::vector<AccessorInfo>& accessors,
    const std::vector<BufferViewInfo>& views,
    const std::vector<std::uint8_t>& bytes,
    std::size_t accessor_index) {
    if (accessor_index >= accessors.size()) {
        fail("accessor index is out of range");
    }
    const AccessorInfo& accessor = accessors[accessor_index];
    if (accessor.buffer_view >= views.size()) {
        fail("accessor bufferView index is out of range");
    }
    const BufferViewInfo& view = views[accessor.buffer_view];

    const std::size_t component_bytes =
        component_size(accessor.component_type);
    std::size_t element_size = 0U;
    if (!checked_mul(
            component_bytes,
            component_count(accessor.type),
            element_size)) {
        fail("accessor element size overflows");
    }
    const std::size_t stride =
        view.byte_stride.value_or(element_size);
    if (stride < element_size || stride % component_bytes != 0U) {
        fail("accessor byteStride is incompatible with element size");
    }
    if (accessor.byte_offset % component_bytes != 0U
        || view.byte_offset % component_bytes != 0U) {
        fail("accessor start is not component-aligned");
    }

    std::size_t view_end = 0U;
    if (!checked_add(view.byte_offset, view.byte_length, view_end)
        || view_end > bytes.size()) {
        fail("bufferView range exceeds external buffer");
    }
    if (accessor.byte_offset > view.byte_length) {
        fail("accessor byteOffset exceeds bufferView");
    }

    std::size_t absolute_start = 0U;
    if (!checked_add(
            view.byte_offset,
            accessor.byte_offset,
            absolute_start)) {
        fail("accessor absolute offset overflows");
    }

    if (accessor.count != 0U) {
        std::size_t preceding = 0U;
        if (!checked_mul(
                accessor.count - 1U,
                stride,
                preceding)) {
            fail("accessor stride range overflows");
        }
        std::size_t relative_end = 0U;
        if (!checked_add(
                accessor.byte_offset,
                preceding,
                relative_end)
            || !checked_add(
                relative_end,
                element_size,
                relative_end)
            || relative_end > view.byte_length) {
            fail("accessor range exceeds bufferView");
        }
    }

    return {
        &accessor,
        &view,
        element_size,
        stride,
        absolute_start,
    };
}

void require_accessor_shape(
    const AccessorWindow& window,
    std::string_view type,
    std::span<const int> component_types,
    std::string_view label,
    bool permit_stride) {
    if (window.accessor->type != type) {
        fail(std::string(label) + " accessor has unsupported type");
    }
    if (std::find(
            component_types.begin(),
            component_types.end(),
            window.accessor->component_type)
        == component_types.end()) {
        fail(std::string(label)
             + " accessor has unsupported componentType");
    }
    if (window.accessor->normalized) {
        fail(std::string(label)
             + " accessor normalized mode is unsupported");
    }
    if (!permit_stride && window.view->byte_stride) {
        fail(std::string(label)
             + " accessor does not permit byteStride");
    }
}

[[nodiscard]] const std::uint8_t* element_pointer(
    const AccessorWindow& window,
    const std::vector<std::uint8_t>& bytes,
    std::size_t index) {
    if (index >= window.accessor->count) {
        fail("accessor element index is out of range");
    }
    std::size_t offset = 0U;
    if (!checked_mul(index, window.stride, offset)
        || !checked_add(
            window.absolute_start,
            offset,
            offset)
        || offset > bytes.size()
        || window.element_size > bytes.size() - offset) {
        fail("accessor element address exceeds external buffer");
    }
    return bytes.data() + offset;
}

[[nodiscard]] std::uint16_t read_u16(
    const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0])
        | (static_cast<std::uint16_t>(data[1]) << 8U));
}

[[nodiscard]] std::uint32_t read_u32(
    const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8U)
        | (static_cast<std::uint32_t>(data[2]) << 16U)
        | (static_cast<std::uint32_t>(data[3]) << 24U);
}

[[nodiscard]] float read_f32(
    const std::uint8_t* data,
    std::string_view label) {
    const float value = std::bit_cast<float>(read_u32(data));
    if (!std::isfinite(value)) {
        fail(std::string(label) + " contains non-finite float data");
    }
    return value;
}

[[nodiscard]] std::array<float, 3> read_vec3(
    const AccessorWindow& window,
    const std::vector<std::uint8_t>& bytes,
    std::size_t index,
    std::string_view label) {
    const std::uint8_t* data =
        element_pointer(window, bytes, index);
    return {
        read_f32(data + 0U, label),
        read_f32(data + 4U, label),
        read_f32(data + 8U, label),
    };
}

[[nodiscard]] std::array<float, 4> read_vec4_f32(
    const AccessorWindow& window,
    const std::vector<std::uint8_t>& bytes,
    std::size_t index,
    std::string_view label) {
    const std::uint8_t* data =
        element_pointer(window, bytes, index);
    return {
        read_f32(data + 0U, label),
        read_f32(data + 4U, label),
        read_f32(data + 8U, label),
        read_f32(data + 12U, label),
    };
}

[[nodiscard]] std::array<std::size_t, 4> read_vec4_joints(
    const AccessorWindow& window,
    const std::vector<std::uint8_t>& bytes,
    std::size_t index) {
    const std::uint8_t* data =
        element_pointer(window, bytes, index);
    if (window.accessor->component_type == 5121) {
        return {
            data[0],
            data[1],
            data[2],
            data[3],
        };
    }
    return {
        read_u16(data + 0U),
        read_u16(data + 2U),
        read_u16(data + 4U),
        read_u16(data + 6U),
    };
}

[[nodiscard]] std::uint32_t read_index(
    const AccessorWindow& window,
    const std::vector<std::uint8_t>& bytes,
    std::size_t index) {
    const std::uint8_t* data =
        element_pointer(window, bytes, index);
    switch (window.accessor->component_type) {
        case 5121:
            return data[0];
        case 5123:
            return read_u16(data);
        case 5125:
            return read_u32(data);
        default:
            fail("index accessor componentType is unsupported");
    }
}

[[nodiscard]] Mat4 read_mat4(
    const AccessorWindow& window,
    const std::vector<std::uint8_t>& bytes,
    std::size_t index,
    std::string_view label) {
    const std::uint8_t* data =
        element_pointer(window, bytes, index);
    Mat4 result{};
    for (std::size_t column = 0U; column < 4U; ++column) {
        for (std::size_t row = 0U; row < 4U; ++row) {
            const std::size_t scalar =
                column * 4U + row;
            result(row, column) =
                read_f32(data + scalar * 4U, label);
        }
    }
    validate_gltf_affine_matrix(result, label);
    return result;
}

[[nodiscard]] VertexSkinBinding make_binding(
    const std::array<std::size_t, 4>& joints,
    const std::array<float, 4>& weights,
    std::size_t joint_count) {
    std::array<SkinInfluence, 4> active{};
    std::size_t active_count = 0U;
    std::set<std::size_t> seen;
    double sum = 0.0;

    for (std::size_t slot = 0U; slot < 4U; ++slot) {
        if (joints[slot] >= joint_count) {
            fail("JOINTS_0 value exceeds skin joint count");
        }
        const float weight = weights[slot];
        if (!std::isfinite(weight) || weight < 0.0F) {
            fail("WEIGHTS_0 must contain finite non-negative values");
        }
        sum += static_cast<double>(weight);
        if (weight == 0.0F) {
            continue;
        }
        if (!seen.insert(joints[slot]).second) {
            fail("JOINTS_0 repeats a non-zero influence for one vertex");
        }
        if (joints[slot]
            > static_cast<std::size_t>(
                std::numeric_limits<std::uint16_t>::max())) {
            fail("joint index exceeds renderer skinning representation");
        }
        active[active_count++] = {
            static_cast<std::uint16_t>(joints[slot]),
            weight,
        };
    }

    if (!std::isfinite(sum)
        || std::fabs(sum - 1.0) > 1.0e-4) {
        fail("WEIGHTS_0 values must sum to one within the bounded importer tolerance");
    }
    switch (active_count) {
        case 1U:
            return VertexSkinBinding({active[0]});
        case 2U:
            return VertexSkinBinding({active[0], active[1]});
        case 3U:
            return VertexSkinBinding({
                active[0], active[1], active[2]});
        case 4U:
            return VertexSkinBinding({
                active[0], active[1], active[2], active[3]});
        default:
            fail("WEIGHTS_0 requires at least one positive influence");
    }
}

[[nodiscard]] std::string validated_external_buffer_uri(
    const JsonValue& value) {
    const std::string& uri = as_string(value, "buffer uri");
    if (uri.empty() || uri.size() > 255U) {
        fail("external buffer uri has invalid bounded length");
    }
    if (!(uri.front() >= 'A' && uri.front() <= 'Z')
        && !(uri.front() >= 'a' && uri.front() <= 'z')
        && !(uri.front() >= '0' && uri.front() <= '9')) {
        fail("external buffer uri must begin with an alphanumeric filename character");
    }
    for (const char c : uri) {
        const bool safe =
            (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9')
            || c == '.' || c == '_' || c == '-';
        if (!safe) {
            fail("external buffer uri must name one sibling file without path or scheme syntax");
        }
    }
    if (uri.find("..") != std::string::npos) {
        fail("external buffer uri must not contain traversal syntax");
    }
    return uri;
}

[[nodiscard]] std::vector<BufferViewInfo> parse_buffer_views(
    const std::map<std::string, JsonValue>& root) {
    const auto& array = as_array(
        require_member(root, "bufferViews", "root"),
        "bufferViews");
    if (array.empty() || array.size() > kMaxGltfBufferViews) {
        fail("bufferViews count is outside bounded importer limits");
    }
    std::vector<BufferViewInfo> result;
    result.reserve(array.size());
    for (const JsonValue& value : array) {
        const auto& object = as_object(value, "bufferView");
        reject_member(object, "extensions", "bufferView");
        const std::size_t buffer = as_index(
            require_member(object, "buffer", "bufferView"),
            "bufferView buffer");
        if (buffer != 0U) {
            fail("bounded importer supports exactly buffer index zero");
        }
        const std::size_t byte_offset =
            optional_member(object, "byteOffset")
            ? as_index(*optional_member(object, "byteOffset"),
                       "bufferView byteOffset")
            : 0U;
        const std::size_t byte_length = as_index(
            require_member(object, "byteLength", "bufferView"),
            "bufferView byteLength");
        if (byte_length == 0U) {
            fail("bufferView byteLength must be non-zero");
        }
        std::optional<std::size_t> byte_stride;
        if (const JsonValue* stride =
                optional_member(object, "byteStride")) {
            const std::size_t parsed =
                as_index(*stride, "bufferView byteStride");
            if (parsed < 4U || parsed > 252U) {
                fail("bufferView byteStride must be within [4, 252]");
            }
            byte_stride = parsed;
        }
        result.push_back({
            byte_offset,
            byte_length,
            byte_stride,
        });
    }
    return result;
}

[[nodiscard]] std::vector<AccessorInfo> parse_accessors(
    const std::map<std::string, JsonValue>& root) {
    const auto& array = as_array(
        require_member(root, "accessors", "root"),
        "accessors");
    if (array.empty() || array.size() > kMaxGltfAccessors) {
        fail("accessor count is outside bounded importer limits");
    }
    std::vector<AccessorInfo> result;
    result.reserve(array.size());
    for (const JsonValue& value : array) {
        const auto& object = as_object(value, "accessor");
        reject_member(object, "sparse", "accessor");
        reject_member(object, "extensions", "accessor");
        const std::size_t buffer_view = as_index(
            require_member(object, "bufferView", "accessor"),
            "accessor bufferView");
        const std::size_t byte_offset =
            optional_member(object, "byteOffset")
            ? as_index(*optional_member(object, "byteOffset"),
                       "accessor byteOffset")
            : 0U;
        const int component_type = as_int(
            require_member(object, "componentType", "accessor"),
            "accessor componentType");
        (void)component_size(component_type);
        const std::size_t count = as_index(
            require_member(object, "count", "accessor"),
            "accessor count");
        if (count == 0U) {
            fail("accessor count must be non-zero");
        }
        const std::string type = as_string(
            require_member(object, "type", "accessor"),
            "accessor type");
        const std::size_t value_components =
            component_count(type);
        const bool normalized =
            optional_member(object, "normalized")
            ? as_bool(*optional_member(object, "normalized"),
                      "accessor normalized")
            : false;
        std::optional<std::vector<float>> min_values;
        if (const JsonValue* minimum =
                optional_member(object, "min")) {
            min_values = number_array(
                *minimum,
                value_components,
                "accessor min");
        }
        std::optional<std::vector<float>> max_values;
        if (const JsonValue* maximum =
                optional_member(object, "max")) {
            max_values = number_array(
                *maximum,
                value_components,
                "accessor max");
        }
        if (min_values.has_value() != max_values.has_value()) {
            fail("accessor min and max must either both be present or both be absent");
        }
        if (min_values) {
            for (std::size_t component = 0U;
                 component < value_components;
                 ++component) {
                if ((*min_values)[component]
                    > (*max_values)[component]) {
                    fail("accessor min must not exceed max");
                }
            }
        }
        result.push_back({
            buffer_view,
            byte_offset,
            component_type,
            count,
            type,
            normalized,
            std::move(min_values),
            std::move(max_values),
        });
    }
    return result;
}

[[nodiscard]] std::vector<NodeInfo> parse_nodes(
    const std::map<std::string, JsonValue>& root) {
    const auto& array = as_array(
        require_member(root, "nodes", "root"),
        "nodes");
    if (array.empty() || array.size() > kMaxGltfNodes) {
        fail("node count is outside bounded importer limits");
    }
    std::vector<NodeInfo> nodes;
    nodes.reserve(array.size());
    for (const JsonValue& value : array) {
        const auto& object = as_object(value, "node");
        reject_member(object, "extensions", "node");
        reject_member(object, "weights", "node");
        reject_member(object, "camera", "node");

        NodeInfo node;
        const ParsedNodeTransform transform =
            parse_node_transform(object);
        node.local = transform.local;
        node.semantic_trs = transform.semantic_trs;
        if (const JsonValue* children =
                optional_member(object, "children")) {
            const auto& child_array =
                as_array(*children, "node children");
            std::set<std::size_t> unique;
            for (const JsonValue& child : child_array) {
                const std::size_t index =
                    as_index(child, "node child");
                if (!unique.insert(index).second) {
                    fail("node children must be unique");
                }
                node.children.push_back(index);
            }
        }
        if (const JsonValue* mesh =
                optional_member(object, "mesh")) {
            node.mesh = as_index(*mesh, "node mesh");
        }
        if (const JsonValue* skin =
                optional_member(object, "skin")) {
            node.skin = as_index(*skin, "node skin");
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

[[nodiscard]] std::vector<std::optional<std::size_t>>
validate_node_hierarchy(
    const std::vector<NodeInfo>& nodes) {
    std::vector<std::optional<std::size_t>> parent(
        nodes.size(),
        std::nullopt);
    for (std::size_t node = 0U; node < nodes.size(); ++node) {
        for (const std::size_t child : nodes[node].children) {
            if (child >= nodes.size()) {
                fail("node child index is out of range");
            }
            if (child == node) {
                fail("node cannot parent itself");
            }
            if (parent[child]) {
                fail("node hierarchy gives one child multiple parents");
            }
            parent[child] = node;
        }
    }

    std::vector<unsigned char> state(nodes.size(), 0U);
    const auto visit = [&](auto&& self, std::size_t node) -> void {
        if (state[node] == 2U) {
            return;
        }
        if (state[node] == 1U) {
            fail("node hierarchy contains a cycle");
        }
        state[node] = 1U;
        for (const std::size_t child : nodes[node].children) {
            self(self, child);
        }
        state[node] = 2U;
    };
    for (std::size_t node = 0U; node < nodes.size(); ++node) {
        visit(visit, node);
    }
    return parent;
}

[[nodiscard]] std::size_t hierarchy_root(
    std::size_t node,
    std::span<const std::optional<std::size_t>> parent) {
    while (parent[node]) {
        node = *parent[node];
    }
    return node;
}

[[nodiscard]] bool is_ancestor(
    std::size_t ancestor,
    std::size_t node,
    std::span<const std::optional<std::size_t>> parent) {
    while (true) {
        if (node == ancestor) {
            return true;
        }
        if (!parent[node]) {
            return false;
        }
        node = *parent[node];
    }
}


struct GltfAnimationSamplerInfo {
    std::size_t input{};
    std::size_t output{};
    SkeletalInterpolationMode interpolation{
        SkeletalInterpolationMode::Linear};
};

[[nodiscard]] std::vector<float> read_animation_times(
    const AccessorWindow& window,
    const std::vector<std::uint8_t>& bytes) {
    const std::array<int, 1> float_type{5126};
    require_accessor_shape(
        window,
        "SCALAR",
        float_type,
        "animation input",
        true);
    if (window.accessor->count == 0U
        || window.accessor->count > kMaxSkeletalTrsTrackKeys) {
        fail("animation input key count is outside bounded track limits");
    }
    if (!window.accessor->min_values
        || !window.accessor->max_values
        || window.accessor->min_values->size() != 1U
        || window.accessor->max_values->size() != 1U) {
        fail("animation input accessor requires scalar min and max");
    }
    std::vector<float> times;
    times.reserve(window.accessor->count);
    for (std::size_t index = 0U;
         index < window.accessor->count;
         ++index) {
        const float time = read_f32(
            element_pointer(window, bytes, index),
            "animation input");
        if (time < 0.0F) {
            fail("animation input time must be non-negative");
        }
        if (index > 0U && !(time > times.back())) {
            fail("animation input times must be strictly increasing");
        }
        times.push_back(time);
    }
    if (times.front() != (*window.accessor->min_values)[0]
        || times.back() != (*window.accessor->max_values)[0]) {
        fail("animation input min/max must match the first and last key times");
    }
    return times;
}

[[nodiscard]] std::shared_ptr<const SkeletalTrsClip>
parse_gltf_animation(
    const JsonValue& animation_value,
    const std::vector<AccessorInfo>& accessors,
    const std::vector<BufferViewInfo>& views,
    const std::vector<std::uint8_t>& bytes,
    const std::vector<NodeInfo>& nodes,
    std::span<const std::optional<std::size_t>> joint_index_by_node,
    SkeletalRigPtr rig,
    const std::vector<SkeletalTrs>& default_pose,
    const std::vector<Mat4>& local_prefixes) {
    const auto& animation =
        as_object(animation_value, "animation");
    reject_member(animation, "extensions", "animation");

    const auto& sampler_values = as_array(
        require_member(animation, "samplers", "animation"),
        "animation samplers");
    const auto& channel_values = as_array(
        require_member(animation, "channels", "animation"),
        "animation channels");
    if (sampler_values.empty()
        || sampler_values.size() > 256U) {
        fail("animation sampler count is outside bounded limits");
    }
    if (channel_values.empty()
        || channel_values.size() > 256U) {
        fail("animation channel count is outside bounded limits");
    }

    std::vector<GltfAnimationSamplerInfo> samplers;
    samplers.reserve(sampler_values.size());
    for (const JsonValue& value : sampler_values) {
        const auto& sampler =
            as_object(value, "animation sampler");
        reject_member(
            sampler,
            "extensions",
            "animation sampler");
        const std::size_t input = as_index(
            require_member(
                sampler,
                "input",
                "animation sampler"),
            "animation sampler input");
        const std::size_t output = as_index(
            require_member(
                sampler,
                "output",
                "animation sampler"),
            "animation sampler output");
        const std::string interpolation =
            optional_member(sampler, "interpolation")
            ? as_string(
                  *optional_member(
                      sampler,
                      "interpolation"),
                  "animation sampler interpolation")
            : "LINEAR";
        SkeletalInterpolationMode interpolation_mode =
            SkeletalInterpolationMode::Linear;
        if (interpolation == "STEP") {
            interpolation_mode =
                SkeletalInterpolationMode::Step;
        } else if (interpolation != "LINEAR") {
            fail("bounded animated importer supports LINEAR or STEP interpolation only");
        }
        if (input >= accessors.size()
            || output >= accessors.size()) {
            fail("animation sampler accessor index is out of range");
        }
        samplers.push_back({
            input,
            output,
            interpolation_mode,
        });
    }

    std::vector<SkeletalTranslationTrack> translations;
    std::vector<SkeletalRotationTrack> rotations;
    std::vector<SkeletalScaleTrack> scales;
    std::vector<bool> translation_seen(
        rig->parents().size(),
        false);
    std::vector<bool> rotation_seen(
        rig->parents().size(),
        false);
    std::vector<bool> scale_seen(
        rig->parents().size(),
        false);
    std::optional<float> clip_start;
    std::optional<float> clip_end;
    const std::array<int, 1> float_type{5126};

    for (const JsonValue& value : channel_values) {
        const auto& channel =
            as_object(value, "animation channel");
        reject_member(
            channel,
            "extensions",
            "animation channel");
        const std::size_t sampler_index = as_index(
            require_member(
                channel,
                "sampler",
                "animation channel"),
            "animation channel sampler");
        if (sampler_index >= samplers.size()) {
            fail("animation channel sampler index is out of range");
        }
        const auto& target = as_object(
            require_member(
                channel,
                "target",
                "animation channel"),
            "animation channel target");
        reject_member(
            target,
            "extensions",
            "animation channel target");
        const std::size_t node = as_index(
            require_member(
                target,
                "node",
                "animation channel target"),
            "animation target node");
        if (node >= nodes.size()) {
            fail("animation target node index is out of range");
        }
        if (node >= joint_index_by_node.size()
            || !joint_index_by_node[node]) {
            fail("animation channel must target an imported skin joint node");
        }
        if (!nodes[node].semantic_trs) {
            fail("animated skin joint must use TRS node state rather than matrix");
        }
        const std::size_t joint =
            *joint_index_by_node[node];
        const std::string path = as_string(
            require_member(
                target,
                "path",
                "animation channel target"),
            "animation target path");
        if (path != "translation"
            && path != "rotation"
            && path != "scale") {
            fail("animation target path is outside translation/rotation/scale");
        }

        const GltfAnimationSamplerInfo& sampler =
            samplers[sampler_index];
        const AccessorWindow input = accessor_window(
            accessors,
            views,
            bytes,
            sampler.input);
        const AccessorWindow output = accessor_window(
            accessors,
            views,
            bytes,
            sampler.output);
        const std::vector<float> times =
            read_animation_times(input, bytes);
        if (output.accessor->count != times.size()) {
            fail("animation sampler input/output key counts must match");
        }
        clip_start = clip_start
            ? std::min(*clip_start, times.front())
            : times.front();
        clip_end = clip_end
            ? std::max(*clip_end, times.back())
            : times.back();

        if (path == "translation" || path == "scale") {
            require_accessor_shape(
                output,
                "VEC3",
                float_type,
                path == "translation"
                    ? "animation translation output"
                    : "animation scale output",
                true);
            std::vector<SkeletalVec3Keyframe> keys;
            keys.reserve(times.size());
            for (std::size_t key = 0U;
                 key < times.size();
                 ++key) {
                const auto value3 = read_vec3(
                    output,
                    bytes,
                    key,
                    path == "translation"
                        ? "animation translation output"
                        : "animation scale output");
                keys.push_back({
                    times[key],
                    {
                        value3[0],
                        value3[1],
                        value3[2],
                    },
                });
            }
            std::vector<bool>& seen =
                path == "translation"
                ? translation_seen
                : scale_seen;
            if (seen[joint]) {
                fail("animation contains duplicate joint/property target ownership");
            }
            seen[joint] = true;
            if (path == "translation") {
                translations.push_back({
                    joint,
                    std::move(keys),
                    sampler.interpolation,
                });
            } else {
                scales.push_back({
                    joint,
                    std::move(keys),
                    sampler.interpolation,
                });
            }
        } else {
            require_accessor_shape(
                output,
                "VEC4",
                float_type,
                "animation rotation output",
                true);
            if (rotation_seen[joint]) {
                fail("animation contains duplicate joint/property target ownership");
            }
            rotation_seen[joint] = true;
            std::vector<SkeletalQuaternionKeyframe> keys;
            keys.reserve(times.size());
            for (std::size_t key = 0U;
                 key < times.size();
                 ++key) {
                const auto value4 = read_vec4_f32(
                    output,
                    bytes,
                    key,
                    "animation rotation output");
                keys.push_back({
                    times[key],
                    {
                        value4[0],
                        value4[1],
                        value4[2],
                        value4[3],
                    },
                });
            }
            rotations.push_back({
                joint,
                std::move(keys),
                sampler.interpolation,
            });
        }
    }

    if (!clip_start || !clip_end
        || !(*clip_end > *clip_start)) {
        fail("animation channels must span a finite increasing clip domain");
    }

    try {
        return std::make_shared<const SkeletalTrsClip>(
            std::move(rig),
            *clip_start,
            *clip_end,
            default_pose,
            local_prefixes,
            std::move(translations),
            std::move(rotations),
            std::move(scales));
    } catch (const std::invalid_argument& error) {
        fail(error.what());
    } catch (const std::out_of_range& error) {
        fail(error.what());
    }
}

struct GltfImportBundle {
    GltfSkinnedAsset asset{};
    std::vector<GltfImportedAnimation> animations{};
};

[[nodiscard]] GltfImportBundle load_gltf_skinned_asset_impl(
    const std::filesystem::path& path,
    bool require_animation) {
    if (path.extension() != ".gltf") {
        fail("bounded importer requires a textual .gltf file");
    }

    const std::string text =
        read_text_file(path, kMaxGltfJsonBytes);
    const JsonValue root_value = JsonParser(text).parse();
    const auto& root = as_object(root_value, "root");

    reject_member(root, "extensions", "root");
    reject_member(root, "extensionsUsed", "root");
    reject_member(root, "extensionsRequired", "root");
    if (!require_animation) {
        reject_member(root, "animations", "root");
    }

    const auto& asset = as_object(
        require_member(root, "asset", "root"),
        "asset");
    if (as_string(
            require_member(asset, "version", "asset"),
            "asset version")
        != "2.0") {
        fail("asset version must be exactly 2.0");
    }

    const auto& buffers = as_array(
        require_member(root, "buffers", "root"),
        "buffers");
    if (buffers.size() != 1U) {
        fail("bounded importer requires exactly one external buffer");
    }
    const auto& buffer_object =
        as_object(buffers.front(), "buffer");
    reject_member(buffer_object, "extensions", "buffer");
    const std::string buffer_uri = validated_external_buffer_uri(
        require_member(buffer_object, "uri", "buffer"));
    const std::size_t declared_buffer_length = as_index(
        require_member(buffer_object, "byteLength", "buffer"),
        "buffer byteLength");
    if (declared_buffer_length == 0U
        || declared_buffer_length > kMaxGltfBinaryBytes) {
        fail("buffer byteLength is outside bounded importer limits");
    }

    const std::filesystem::path buffer_path =
        path.parent_path() / buffer_uri;
    const std::vector<std::uint8_t> bytes =
        read_binary_file(buffer_path, kMaxGltfBinaryBytes);
    if (bytes.size() != declared_buffer_length) {
        fail("external buffer size does not match declared byteLength");
    }

    const std::vector<BufferViewInfo> views =
        parse_buffer_views(root);
    const std::vector<AccessorInfo> accessors =
        parse_accessors(root);
    for (std::size_t index = 0U;
         index < accessors.size();
         ++index) {
        (void)accessor_window(
            accessors,
            views,
            bytes,
            index);
    }

    const auto& meshes = as_array(
        require_member(root, "meshes", "root"),
        "meshes");
    if (meshes.size() != 1U) {
        fail("bounded importer requires exactly one mesh");
    }
    const auto& mesh_object =
        as_object(meshes.front(), "mesh");
    reject_member(mesh_object, "extensions", "mesh");
    reject_member(mesh_object, "weights", "mesh");
    const auto& primitives = as_array(
        require_member(mesh_object, "primitives", "mesh"),
        "mesh primitives");
    if (primitives.size() != 1U) {
        fail("bounded importer requires exactly one mesh primitive");
    }
    const auto& primitive =
        as_object(primitives.front(), "mesh primitive");
    reject_member(primitive, "extensions", "mesh primitive");
    reject_member(primitive, "targets", "mesh primitive");
    reject_member(primitive, "material", "mesh primitive");
    const int mode =
        optional_member(primitive, "mode")
        ? as_int(*optional_member(primitive, "mode"),
                 "mesh primitive mode")
        : 4;
    if (mode != 4) {
        fail("bounded importer supports TRIANGLES mode only");
    }

    const std::size_t index_accessor = as_index(
        require_member(primitive, "indices", "mesh primitive"),
        "mesh primitive indices");
    const auto& attributes = as_object(
        require_member(primitive, "attributes", "mesh primitive"),
        "mesh primitive attributes");
    for (const auto& [name, value] : attributes) {
        (void)value;
        if (name != "POSITION"
            && name != "NORMAL"
            && name != "JOINTS_0"
            && name != "WEIGHTS_0") {
            fail("mesh primitive attribute '" + name
                 + "' is outside the bounded importer subset");
        }
    }
    const std::size_t position_accessor = as_index(
        require_member(attributes, "POSITION", "mesh attributes"),
        "POSITION accessor");
    const std::size_t joints_accessor = as_index(
        require_member(attributes, "JOINTS_0", "mesh attributes"),
        "JOINTS_0 accessor");
    const std::size_t weights_accessor = as_index(
        require_member(attributes, "WEIGHTS_0", "mesh attributes"),
        "WEIGHTS_0 accessor");
    std::optional<std::size_t> normal_accessor;
    if (const JsonValue* normal =
            optional_member(attributes, "NORMAL")) {
        normal_accessor = as_index(*normal, "NORMAL accessor");
    }

    const AccessorWindow positions = accessor_window(
        accessors, views, bytes, position_accessor);
    const std::array<int, 1> float_type{5126};
    require_accessor_shape(
        positions,
        "VEC3",
        float_type,
        "POSITION",
        true);
    const std::size_t vertex_count = positions.accessor->count;
    if (vertex_count < 3U || vertex_count > kMaxGltfVertices) {
        fail("POSITION count is outside bounded importer limits");
    }
    if (!positions.accessor->min_values
        || !positions.accessor->max_values) {
        fail("POSITION accessor requires min and max");
    }

    std::optional<AccessorWindow> normals;
    if (normal_accessor) {
        normals = accessor_window(
            accessors, views, bytes, *normal_accessor);
        require_accessor_shape(
            *normals,
            "VEC3",
            float_type,
            "NORMAL",
            true);
        if (normals->accessor->count != vertex_count) {
            fail("NORMAL count must match POSITION count");
        }
    }

    const AccessorWindow joints = accessor_window(
        accessors, views, bytes, joints_accessor);
    const std::array<int, 2> joint_types{5121, 5123};
    require_accessor_shape(
        joints,
        "VEC4",
        joint_types,
        "JOINTS_0",
        true);
    if (joints.accessor->count != vertex_count) {
        fail("JOINTS_0 count must match POSITION count");
    }

    const AccessorWindow weights = accessor_window(
        accessors, views, bytes, weights_accessor);
    require_accessor_shape(
        weights,
        "VEC4",
        float_type,
        "WEIGHTS_0",
        true);
    if (weights.accessor->count != vertex_count) {
        fail("WEIGHTS_0 count must match POSITION count");
    }

    const AccessorWindow indices = accessor_window(
        accessors, views, bytes, index_accessor);
    const std::array<int, 3> index_types{5121, 5123, 5125};
    require_accessor_shape(
        indices,
        "SCALAR",
        index_types,
        "indices",
        false);
    if (indices.accessor->count == 0U
        || indices.accessor->count > kMaxGltfIndices
        || indices.accessor->count % 3U != 0U) {
        fail("index count must be a bounded positive multiple of three");
    }

    const std::vector<NodeInfo> nodes = parse_nodes(root);
    const std::vector<std::optional<std::size_t>> node_parent =
        validate_node_hierarchy(nodes);

    std::optional<std::size_t> mesh_skin_node;
    for (std::size_t node = 0U; node < nodes.size(); ++node) {
        const bool has_mesh = nodes[node].mesh.has_value();
        const bool has_skin = nodes[node].skin.has_value();
        if (has_mesh != has_skin) {
            fail("bounded importer requires mesh and skin to be paired on one node");
        }
        if (!has_mesh) {
            continue;
        }
        if (*nodes[node].mesh != 0U || *nodes[node].skin != 0U) {
            fail("bounded importer supports mesh index zero with skin index zero only");
        }
        if (mesh_skin_node) {
            fail("bounded importer supports exactly one skinned mesh instance");
        }
        mesh_skin_node = node;
    }
    if (!mesh_skin_node) {
        fail("bounded importer requires one node instantiating mesh zero with skin zero");
    }

    const auto& skins = as_array(
        require_member(root, "skins", "root"),
        "skins");
    if (skins.size() != 1U) {
        fail("bounded importer requires exactly one skin");
    }
    const auto& skin = as_object(skins.front(), "skin");
    reject_member(skin, "extensions", "skin");
    const auto& joint_values = as_array(
        require_member(skin, "joints", "skin"),
        "skin joints");
    if (joint_values.empty()
        || joint_values.size() > kMaxSkinJoints) {
        fail("skin joint count is outside renderer limits");
    }

    std::vector<std::size_t> joint_nodes;
    joint_nodes.reserve(joint_values.size());
    std::set<std::size_t> unique_joint_nodes;
    for (const JsonValue& value : joint_values) {
        const std::size_t node =
            as_index(value, "skin joint node");
        if (node >= nodes.size()) {
            fail("skin joint node index is out of range");
        }
        if (!unique_joint_nodes.insert(node).second) {
            fail("skin joint node indices must be unique");
        }
        if (node == *mesh_skin_node) {
            fail("skinned mesh instance node cannot also be a joint in this bounded subset");
        }
        joint_nodes.push_back(node);
    }

    const std::size_t common_root =
        hierarchy_root(joint_nodes.front(), node_parent);
    for (const std::size_t node : joint_nodes) {
        if (hierarchy_root(node, node_parent) != common_root) {
            fail("skin joints must share one node-tree root");
        }
        if (is_ancestor(*mesh_skin_node, node, node_parent)) {
            fail("skinned mesh instance cannot be an ancestor of a joint in this bounded subset");
        }
    }

    if (const JsonValue* skeleton =
            optional_member(skin, "skeleton")) {
        const std::size_t skeleton_node =
            as_index(*skeleton, "skin skeleton");
        if (skeleton_node >= nodes.size()) {
            fail("skin skeleton node index is out of range");
        }
        for (const std::size_t node : joint_nodes) {
            if (!is_ancestor(
                    skeleton_node,
                    node,
                    node_parent)) {
                fail("skin skeleton node must be an ancestor of every joint");
            }
        }
    }

    const std::size_t inverse_accessor_index = as_index(
        require_member(skin, "inverseBindMatrices", "skin"),
        "skin inverseBindMatrices");
    const AccessorWindow inverse_binds = accessor_window(
        accessors,
        views,
        bytes,
        inverse_accessor_index);
    require_accessor_shape(
        inverse_binds,
        "MAT4",
        float_type,
        "inverseBindMatrices",
        false);
    if (inverse_binds.accessor->count != joint_nodes.size()) {
        fail("inverseBindMatrices count must exactly match skin joint count in the bounded subset");
    }

    std::vector<Mat4> inverse_bind_matrices;
    inverse_bind_matrices.reserve(joint_nodes.size());
    for (std::size_t joint = 0U;
         joint < joint_nodes.size();
         ++joint) {
        inverse_bind_matrices.push_back(
            read_mat4(
                inverse_binds,
                bytes,
                joint,
                "glTF inverse-bind matrix"));
    }

    std::vector<std::optional<std::size_t>> joint_index_by_node(
        nodes.size(),
        std::nullopt);
    for (std::size_t joint = 0U;
         joint < joint_nodes.size();
         ++joint) {
        joint_index_by_node[joint_nodes[joint]] = joint;
    }

    std::vector<std::optional<std::size_t>> rig_parents(
        joint_nodes.size(),
        std::nullopt);
    std::vector<Mat4> rest_locals(
        joint_nodes.size(),
        Mat4::identity());
    std::vector<SkeletalTrs> semantic_defaults(
        joint_nodes.size(),
        SkeletalTrs{});
    std::vector<Mat4> local_prefixes(
        joint_nodes.size(),
        Mat4::identity());
    for (std::size_t joint = 0U;
         joint < joint_nodes.size();
         ++joint) {
        const std::size_t joint_node = joint_nodes[joint];
        std::size_t cursor = joint_node;
        Mat4 prefix = Mat4::identity();
        std::optional<std::size_t> ancestor =
            node_parent[cursor];
        while (ancestor && !joint_index_by_node[*ancestor]) {
            prefix = nodes[*ancestor].local * prefix;
            cursor = *ancestor;
            ancestor = node_parent[cursor];
        }
        if (ancestor) {
            rig_parents[joint] =
                *joint_index_by_node[*ancestor];
        }

        Mat4 rest_local;
        if (nodes[joint_node].semantic_trs) {
            semantic_defaults[joint] =
                *nodes[joint_node].semantic_trs;
            local_prefixes[joint] = prefix;
            rest_local =
                prefix * nodes[joint_node].local;
        } else {
            // Matrix-backed joints are valid static joints. Their full own
            // matrix becomes part of the immutable prefix; animation channels
            // targeting such a joint are rejected by the animated importer.
            local_prefixes[joint] =
                prefix * nodes[joint_node].local;
            rest_local = local_prefixes[joint];
        }

        validate_gltf_affine_matrix(
            local_prefixes[joint],
            "glTF joint immutable local prefix");
        validate_gltf_affine_matrix(
            rest_local,
            "glTF compressed joint local transform");
        rest_locals[joint] = rest_local;
    }

    std::vector<VertexSkinBinding> vertex_bindings;
    vertex_bindings.reserve(vertex_count);
    ModelAsset model;
    model.mesh.vertices.reserve(vertex_count);
    for (std::size_t vertex = 0U;
         vertex < vertex_count;
         ++vertex) {
        const auto position =
            read_vec3(positions, bytes, vertex, "POSITION");
        for (std::size_t component = 0U;
             component < 3U;
             ++component) {
            if (position[component]
                    < (*positions.accessor->min_values)[component]
                || position[component]
                    > (*positions.accessor->max_values)[component]) {
                fail("POSITION data lies outside declared accessor min/max");
            }
        }
        VaryingPack varyings;
        if (normals) {
            const auto normal =
                read_vec3(*normals, bytes, vertex, "NORMAL");
            const double length_squared =
                static_cast<double>(normal[0]) * normal[0]
                + static_cast<double>(normal[1]) * normal[1]
                + static_cast<double>(normal[2]) * normal[2];
            const double epsilon_squared =
                static_cast<double>(kEpsilon)
                * static_cast<double>(kEpsilon);
            if (!std::isfinite(length_squared)
                || length_squared <= epsilon_squared) {
                fail("NORMAL contains a zero-length vector");
            }
            varyings = VaryingPack{
                normal[0], normal[1], normal[2]};
        }
        model.mesh.vertices.push_back(
            Vertex::with_varyings(
                {position[0], position[1], position[2]},
                varyings));

        vertex_bindings.push_back(
            make_binding(
                read_vec4_joints(joints, bytes, vertex),
                read_vec4_f32(
                    weights,
                    bytes,
                    vertex,
                    "WEIGHTS_0"),
                joint_nodes.size()));
    }

    model.mesh.triangles.reserve(
        indices.accessor->count / 3U);
    for (std::size_t index = 0U;
         index < indices.accessor->count;
         index += 3U) {
        const std::uint32_t a =
            read_index(indices, bytes, index);
        const std::uint32_t b =
            read_index(indices, bytes, index + 1U);
        const std::uint32_t c =
            read_index(indices, bytes, index + 2U);
        if (a >= vertex_count || b >= vertex_count
            || c >= vertex_count) {
            fail("triangle index references a vertex outside POSITION");
        }
        model.mesh.triangles.push_back({a, b, c});
    }

    MaterialDraw draw;
    draw.range = {
        0U,
        model.mesh.triangles.size(),
    };
    model.draws.push_back(std::move(draw));

    SkeletalRigPtr rig;
    try {
        rig = std::make_shared<const SkeletalRig>(
            std::move(rig_parents),
            std::move(inverse_bind_matrices),
            std::move(vertex_bindings));
        // The imported rest pose must already be executable under M108.
        (void)rig->resolve_pose(rest_locals);
    } catch (const std::invalid_argument& error) {
        fail(error.what());
    } catch (const std::out_of_range& error) {
        fail(error.what());
    }

    GltfSkinnedAsset result;
    result.model = std::move(model);
    result.rig = rig;
    result.rest_local_transforms = std::move(rest_locals);
    if (normals) {
        result.normal_channels =
            std::array<std::size_t, 3>{0U, 1U, 2U};
    }

    std::vector<GltfImportedAnimation> animations;
    if (require_animation) {
        const auto& animation_values = as_array(
            require_member(root, "animations", "root"),
            "animations");
        if (animation_values.empty()
            || animation_values.size() > kMaxGltfAnimations) {
            fail("animation collection count is outside bounded limits");
        }
        animations.reserve(animation_values.size());
        for (const JsonValue& animation_value : animation_values) {
            const auto& animation_object =
                as_object(animation_value, "animation");
            std::optional<std::string> name;
            if (const JsonValue* animation_name =
                    optional_member(animation_object, "name")) {
                name = as_string(
                    *animation_name,
                    "animation name");
            }
            std::shared_ptr<const SkeletalTrsClip> clip =
                parse_gltf_animation(
                    animation_value,
                    accessors,
                    views,
                    bytes,
                    nodes,
                    joint_index_by_node,
                    rig,
                    semantic_defaults,
                    local_prefixes);
            animations.push_back({
                std::move(name),
                std::move(clip),
            });
        }
    }
    return {
        std::move(result),
        std::move(animations),
    };
}

}  // namespace

GltfSkinnedAsset load_gltf_skinned_asset_file(
    const std::filesystem::path& path) {
    return load_gltf_skinned_asset_impl(
        path,
        false).asset;
}

GltfSkinnedAnimationCollection
load_gltf_skinned_animation_collection_file(
    const std::filesystem::path& path) {
    GltfImportBundle imported =
        load_gltf_skinned_asset_impl(
            path,
            true);
    return {
        std::move(imported.asset),
        std::move(imported.animations),
    };
}

GltfSkinnedAnimatedAsset
load_gltf_skinned_animated_asset_file(
    const std::filesystem::path& path) {
    GltfSkinnedAnimationCollection imported =
        load_gltf_skinned_animation_collection_file(path);
    if (imported.animations.size() != 1U) {
        fail("exactly-one animated importer requires exactly one animation");
    }
    return {
        std::move(imported.asset),
        std::move(imported.animations.front().clip),
    };
}

}  // namespace tiny_renderer
