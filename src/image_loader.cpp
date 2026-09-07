#include "tiny_renderer/image_loader.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "tiny_renderer/pfm_loader.hpp"
#include "tiny_renderer/ppm_loader.hpp"
#include "tiny_renderer/tga_loader.hpp"

namespace tiny_renderer {
namespace {

constexpr std::size_t kMaxPfmRasterBytes = 64U * 1024U * 1024U;

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);

[[noreturn]] void fail_pfm(const std::string& message) {
    throw PfmParseError("PFM: " + message);
}

bool is_ascii_whitespace(int value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r'
        || value == '\f' || value == '\v';
}

void skip_pfm_header_separators(std::istream& input) {
    for (;;) {
        const int next = input.peek();
        if (next == std::char_traits<char>::eof()) {
            fail_pfm("unexpected end of header");
        }
        if (is_ascii_whitespace(next)) {
            (void)input.get();
            continue;
        }
        if (next == '#') {
            (void)input.get();
            for (;;) {
                const int comment_byte = input.get();
                if (comment_byte == std::char_traits<char>::eof() || comment_byte == '\n') {
                    break;
                }
            }
            continue;
        }
        return;
    }
}

std::string read_pfm_header_token(std::istream& input, const char* field) {
    skip_pfm_header_separators(input);
    std::string token;
    for (;;) {
        const int next = input.peek();
        if (next == std::char_traits<char>::eof() || is_ascii_whitespace(next)) {
            break;
        }
        token.push_back(static_cast<char>(input.get()));
        if (token.size() > 64U) {
            fail_pfm(std::string(field) + " token is too long");
        }
    }
    if (token.empty()) {
        fail_pfm(std::string("missing ") + field);
    }
    return token;
}

std::size_t parse_pfm_dimension(const std::string& token, const char* field) {
    std::uint64_t value{};
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end || value == 0U) {
        fail_pfm(std::string("invalid positive ") + field + " '" + token + "'");
    }
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        fail_pfm(std::string(field) + " exceeds platform size range");
    }
    return static_cast<std::size_t>(value);
}

enum class PfmByteOrder {
    LittleEndian,
    BigEndian,
};

PfmByteOrder parse_pfm_byte_order(const std::string& token) {
    if (token == "-1.0") {
        return PfmByteOrder::LittleEndian;
    }
    if (token == "1.0") {
        return PfmByteOrder::BigEndian;
    }
    fail_pfm("only unit-magnitude scale markers -1.0 and 1.0 are supported");
}

std::size_t checked_pfm_raster_bytes(std::size_t width, std::size_t height) {
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        fail_pfm("image dimensions overflow pixel count");
    }
    const std::size_t pixel_count = width * height;
    constexpr std::size_t bytes_per_pixel = 3U * sizeof(float);
    if (pixel_count > std::numeric_limits<std::size_t>::max() / bytes_per_pixel) {
        fail_pfm("image dimensions overflow RGB float byte count");
    }
    const std::size_t raster_bytes = pixel_count * bytes_per_pixel;
    if (raster_bytes > kMaxPfmRasterBytes) {
        fail_pfm("raster exceeds 64 MiB decoder safety bound");
    }
    if (raster_bytes > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        fail_pfm("raster exceeds streamsize range");
    }
    return raster_bytes;
}

std::uint32_t decode_u32(
    const std::vector<unsigned char>& bytes,
    std::size_t offset,
    PfmByteOrder byte_order) {
    if (byte_order == PfmByteOrder::LittleEndian) {
        return static_cast<std::uint32_t>(bytes[offset])
            | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U)
            | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U)
            | (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U)
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U)
        | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

float decode_pfm_float(
    const std::vector<unsigned char>& bytes,
    std::size_t offset,
    PfmByteOrder byte_order) {
    const float value = std::bit_cast<float>(decode_u32(bytes, offset, byte_order));
    if (!std::isfinite(value)) {
        fail_pfm("raster contains a non-finite RGB value");
    }
    return value;
}

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](char value) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
        });
    return extension;
}

}  // namespace

Texture2D load_pfm(std::istream& input) {
    const std::string magic = read_pfm_header_token(input, "magic");
    if (magic != "PF") {
        fail_pfm("only RGB PF images are supported");
    }

    const std::size_t width = parse_pfm_dimension(
        read_pfm_header_token(input, "width"),
        "width");
    const std::size_t height = parse_pfm_dimension(
        read_pfm_header_token(input, "height"),
        "height");
    const PfmByteOrder byte_order = parse_pfm_byte_order(
        read_pfm_header_token(input, "scale"));
    const std::size_t raster_bytes = checked_pfm_raster_bytes(width, height);

    const int separator = input.get();
    if (separator == std::char_traits<char>::eof() || !is_ascii_whitespace(separator)) {
        fail_pfm("scale marker must be followed by an ASCII whitespace raster separator");
    }

    std::vector<unsigned char> bytes(raster_bytes);
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(raster_bytes));
    if (input.gcount() != static_cast<std::streamsize>(raster_bytes)) {
        fail_pfm("raster payload is truncated");
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        fail_pfm("trailing bytes after raster payload are not supported");
    }
    if (input.bad()) {
        fail_pfm("I/O failure while reading raster payload");
    }

    const std::size_t pixel_count = width * height;
    std::vector<Vec3> texels(pixel_count);
    constexpr std::size_t bytes_per_channel = sizeof(float);
    constexpr std::size_t bytes_per_pixel = 3U * bytes_per_channel;
    for (std::size_t file_row = 0U; file_row < height; ++file_row) {
        const std::size_t texture_row = height - 1U - file_row;
        for (std::size_t x = 0U; x < width; ++x) {
            const std::size_t source = (file_row * width + x) * bytes_per_pixel;
            texels[texture_row * width + x] = {
                decode_pfm_float(bytes, source, byte_order),
                decode_pfm_float(bytes, source + bytes_per_channel, byte_order),
                decode_pfm_float(bytes, source + 2U * bytes_per_channel, byte_order),
            };
        }
    }

    return Texture2D(
        width,
        height,
        std::move(texels),
        TextureTransferFunction::Linear);
}

Texture2D load_pfm_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open PFM file: " + path.string());
    }
    return load_pfm(input);
}

Texture2D load_texture_image_file(
    const std::filesystem::path& path,
    TextureTransferFunction transfer_function) {
    validate_texture_transfer_function(transfer_function);
    const std::string extension = lowercase_extension(path);
    if (extension == ".ppm") {
        return load_ppm_file(path, transfer_function);
    }
    if (extension == ".tga") {
        return load_tga_file(path, transfer_function);
    }
    if (extension == ".pfm") {
        if (transfer_function != TextureTransferFunction::Linear) {
            throw std::invalid_argument(
                "PFM texture images require linear transfer interpretation");
        }
        return load_pfm_file(path);
    }
    throw std::invalid_argument(
        "unsupported texture image extension '" + extension + "' for file: " + path.string());
}

}  // namespace tiny_renderer
