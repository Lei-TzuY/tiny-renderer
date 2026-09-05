#include "tiny_renderer/ppm_loader.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace tiny_renderer {

namespace {

constexpr std::size_t kMaxPpmRasterBytes = 64U * 1024U * 1024U;

[[noreturn]] void fail(const std::string& message) {
    throw PpmParseError("PPM: " + message);
}

bool is_ascii_whitespace(int value) {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r'
        || value == '\f' || value == '\v';
}

void skip_header_separators(std::istream& input) {
    for (;;) {
        const int next = input.peek();
        if (next == std::char_traits<char>::eof()) {
            fail("unexpected end of header");
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

std::string read_header_token(std::istream& input, const char* field) {
    skip_header_separators(input);
    std::string token;
    for (;;) {
        const int next = input.peek();
        if (next == std::char_traits<char>::eof() || is_ascii_whitespace(next)) {
            break;
        }
        token.push_back(static_cast<char>(input.get()));
        if (token.size() > 64U) {
            fail(std::string(field) + " token is too long");
        }
    }
    if (token.empty()) {
        fail(std::string("missing ") + field);
    }
    return token;
}

std::size_t parse_dimension(const std::string& token, const char* field) {
    std::uint64_t value{};
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end || value == 0U) {
        fail(std::string("invalid positive ") + field + " '" + token + "'");
    }
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        fail(std::string(field) + " exceeds platform size range");
    }
    return static_cast<std::size_t>(value);
}

void require_maxval_255(const std::string& token) {
    std::uint32_t value{};
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end || value != 255U) {
        fail("only maxval 255 is supported");
    }
}

std::size_t checked_raster_bytes(std::size_t width, std::size_t height) {
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        fail("image dimensions overflow pixel count");
    }
    const std::size_t pixel_count = width * height;
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 3U) {
        fail("image dimensions overflow RGB byte count");
    }
    const std::size_t raster_bytes = pixel_count * 3U;
    if (raster_bytes > kMaxPpmRasterBytes) {
        fail("raster exceeds 64 MiB decoder safety bound");
    }
    return raster_bytes;
}

}  // namespace

Texture2D load_ppm(std::istream& input) {
    const std::string magic = read_header_token(input, "magic");
    if (magic != "P6") {
        fail("only binary P6 images are supported");
    }

    const std::size_t width = parse_dimension(read_header_token(input, "width"), "width");
    const std::size_t height = parse_dimension(read_header_token(input, "height"), "height");
    require_maxval_255(read_header_token(input, "maxval"));
    const std::size_t raster_bytes = checked_raster_bytes(width, height);

    const int separator = input.get();
    if (separator == std::char_traits<char>::eof() || !is_ascii_whitespace(separator)) {
        fail("maxval must be followed by exactly one ASCII whitespace raster separator");
    }

    std::vector<unsigned char> bytes(raster_bytes);
    if (raster_bytes != 0U) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(raster_bytes));
        if (input.gcount() != static_cast<std::streamsize>(raster_bytes)) {
            fail("raster payload is truncated");
        }
    }

    if (input.peek() != std::char_traits<char>::eof()) {
        fail("trailing bytes after raster payload are not supported");
    }
    if (input.bad()) {
        fail("I/O failure while reading raster payload");
    }

    std::vector<Vec3> texels;
    texels.reserve(width * height);
    constexpr float scale = 1.0F / 255.0F;
    for (std::size_t offset = 0U; offset < bytes.size(); offset += 3U) {
        texels.push_back({
            static_cast<float>(bytes[offset]) * scale,
            static_cast<float>(bytes[offset + 1U]) * scale,
            static_cast<float>(bytes[offset + 2U]) * scale,
        });
    }
    return Texture2D(width, height, std::move(texels));
}

Texture2D load_ppm_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open PPM file: " + path.string());
    }
    return load_ppm(input);
}

}  // namespace tiny_renderer
