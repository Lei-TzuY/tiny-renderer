#include "tiny_renderer/tga_loader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace tiny_renderer {
namespace {

constexpr std::size_t kTgaHeaderBytes = 18U;
constexpr std::size_t kMaxTgaRasterBytes = 64U * 1024U * 1024U;

[[noreturn]] void fail(const std::string& message) {
    throw TgaParseError("TGA: " + message);
}

std::uint16_t little_endian_u16(const std::array<unsigned char, kTgaHeaderBytes>& header, std::size_t offset) {
    return static_cast<std::uint16_t>(header[offset])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(header[offset + 1U]) << 8U);
}

std::size_t checked_raster_bytes(std::size_t width, std::size_t height) {
    if (width == 0U || height == 0U) {
        fail("image dimensions must be non-zero");
    }
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        fail("image dimensions overflow pixel count");
    }
    const std::size_t pixel_count = width * height;
    if (pixel_count > std::numeric_limits<std::size_t>::max() / 3U) {
        fail("image dimensions overflow RGB byte count");
    }
    const std::size_t raster_bytes = pixel_count * 3U;
    if (raster_bytes > kMaxTgaRasterBytes) {
        fail("raster exceeds 64 MiB decoder safety bound");
    }
    return raster_bytes;
}

void read_exact(std::istream& input, unsigned char* data, std::size_t count, const char* label) {
    if (count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        fail(std::string(label) + " exceeds stream size range");
    }
    input.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(count));
    if (input.gcount() != static_cast<std::streamsize>(count)) {
        fail(std::string(label) + " is truncated");
    }
    if (input.bad()) {
        fail(std::string("I/O failure while reading ") + label);
    }
}

}  // namespace

Texture2D load_tga(std::istream& input) {
    std::array<unsigned char, kTgaHeaderBytes> header{};
    read_exact(input, header.data(), header.size(), "header");

    const std::uint8_t id_length = header[0];
    const std::uint8_t color_map_type = header[1];
    const std::uint8_t image_type = header[2];
    if (color_map_type != 0U) {
        fail("color-mapped images are not supported");
    }
    for (std::size_t offset = 3U; offset <= 7U; ++offset) {
        if (header[offset] != 0U) {
            fail("color-map specification must be zero when no color map is present");
        }
    }
    if (image_type != 2U) {
        fail("only uncompressed true-color image type 2 is supported");
    }
    if (little_endian_u16(header, 8U) != 0U || little_endian_u16(header, 10U) != 0U) {
        fail("non-zero image origin coordinates are not supported");
    }

    const std::size_t width = little_endian_u16(header, 12U);
    const std::size_t height = little_endian_u16(header, 14U);
    const std::size_t raster_bytes = checked_raster_bytes(width, height);
    if (header[16] != 24U) {
        fail("only 24-bit BGR pixels are supported");
    }

    const std::uint8_t descriptor = header[17];
    if ((descriptor & 0x0FU) != 0U) {
        fail("attribute bits are not supported for 24-bit images");
    }
    if ((descriptor & 0x10U) != 0U) {
        fail("right-to-left pixel order is not supported");
    }
    if ((descriptor & 0xC0U) != 0U) {
        fail("interleaved pixel order is not supported");
    }
    const bool top_origin = (descriptor & 0x20U) != 0U;

    if (id_length != 0U) {
        std::vector<unsigned char> id_bytes(id_length);
        read_exact(input, id_bytes.data(), id_bytes.size(), "image ID field");
    }

    std::vector<unsigned char> bytes(raster_bytes);
    read_exact(input, bytes.data(), bytes.size(), "raster payload");
    if (input.peek() != std::char_traits<char>::eof()) {
        fail("trailing bytes after raster payload are not supported");
    }
    if (input.bad()) {
        fail("I/O failure after raster payload");
    }

    std::vector<Vec3> texels(width * height);
    constexpr float scale = 1.0F / 255.0F;
    for (std::size_t y = 0U; y < height; ++y) {
        const std::size_t source_y = top_origin ? y : height - 1U - y;
        for (std::size_t x = 0U; x < width; ++x) {
            const std::size_t source = (source_y * width + x) * 3U;
            texels[y * width + x] = {
                static_cast<float>(bytes[source + 2U]) * scale,
                static_cast<float>(bytes[source + 1U]) * scale,
                static_cast<float>(bytes[source]) * scale,
            };
        }
    }
    return Texture2D(width, height, std::move(texels));
}

Texture2D load_tga_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open TGA file: " + path.string());
    }
    return load_tga(input);
}

}  // namespace tiny_renderer
