#include "tiny_renderer/framebuffer.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace tiny_renderer {
namespace {

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);

float linear_channel_to_srgb(float linear) {
    if (!std::isfinite(linear)) {
        throw std::invalid_argument("sRGB output requires finite resolved framebuffer color");
    }
    const float clamped = std::clamp(linear, 0.0F, 1.0F);
    if (clamped <= 0.0031308F) {
        return 12.92F * clamped;
    }
    return 1.055F * static_cast<float>(std::pow(clamped, 1.0F / 2.4F)) - 0.055F;
}

std::uint8_t srgb_to_u8(float linear) {
    const float encoded = std::clamp(linear_channel_to_srgb(linear), 0.0F, 1.0F);
    return static_cast<std::uint8_t>(std::lround(encoded * 255.0F));
}

void append_float_little_endian(std::vector<std::uint8_t>& bytes, float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((bits >> 24U) & 0xFFU));
}

std::vector<std::uint8_t> pfm_payload(
    const std::vector<Vec3>& resolved_color,
    std::size_t width,
    std::size_t height) {
    for (const Vec3& pixel : resolved_color) {
        if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(pixel.z)) {
            throw std::invalid_argument("PFM output requires finite resolved framebuffer color");
        }
    }

    constexpr std::size_t bytes_per_pixel = 3U * sizeof(float);
    if (resolved_color.size() > std::numeric_limits<std::size_t>::max() / bytes_per_pixel) {
        throw std::overflow_error("PFM output size overflows size_t");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(resolved_color.size() * bytes_per_pixel);

    // PFM stores rows bottom-to-top. The -1.0 scale marker written by
    // write_pfm declares little-endian payload floats, so bytes are emitted
    // explicitly rather than depending on host byte order.
    for (std::size_t row = height; row-- > 0U;) {
        const std::size_t row_begin = row * width;
        for (std::size_t x = 0U; x < width; ++x) {
            const Vec3& pixel = resolved_color[row_begin + x];
            append_float_little_endian(bytes, pixel.x);
            append_float_little_endian(bytes, pixel.y);
            append_float_little_endian(bytes, pixel.z);
        }
    }
    return bytes;
}

void validate_stream_write_size(std::size_t size, const char* label) {
    if (size > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::overflow_error(std::string(label) + " exceeds streamsize");
    }
}

}  // namespace

void validate_output_transfer_function(OutputTransferFunction transfer_function) {
    switch (transfer_function) {
        case OutputTransferFunction::Linear:
        case OutputTransferFunction::Srgb:
            return;
    }
    throw std::invalid_argument("unsupported framebuffer output transfer function");
}

std::vector<std::uint8_t> Framebuffer::rgb8(OutputTransferFunction transfer_function) const {
    validate_output_transfer_function(transfer_function);
    if (transfer_function == OutputTransferFunction::Linear) {
        return rgb8();
    }

    if (resolved_color_.size() > std::numeric_limits<std::size_t>::max() / 3U) {
        throw std::overflow_error("RGB8 output size overflows size_t");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(resolved_color_.size() * 3U);
    for (const Vec3& pixel : resolved_color_) {
        bytes.push_back(srgb_to_u8(pixel.x));
        bytes.push_back(srgb_to_u8(pixel.y));
        bytes.push_back(srgb_to_u8(pixel.z));
    }
    return bytes;
}

std::uint64_t Framebuffer::fnv1a64(OutputTransferFunction transfer_function) const {
    validate_output_transfer_function(transfer_function);
    if (transfer_function == OutputTransferFunction::Linear) {
        return fnv1a64();
    }

    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    for (const std::uint8_t byte : rgb8(transfer_function)) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= prime;
    }
    return hash;
}

void Framebuffer::write_ppm(
    const std::string& path,
    OutputTransferFunction transfer_function) const {
    validate_output_transfer_function(transfer_function);
    if (transfer_function == OutputTransferFunction::Linear) {
        write_ppm(path);
        return;
    }

    // Complete validation and encoding before creating or truncating the file.
    // This keeps the explicit sRGB export path fail-closed for invalid resolved
    // framebuffer values.
    const std::vector<std::uint8_t> bytes = rgb8(transfer_function);

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open PPM output: " + path);
    }
    out << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw std::runtime_error("failed while writing PPM output: " + path);
    }
}

void Framebuffer::write_pfm(const std::string& path) const {
    // Build and validate the complete payload before touching the destination.
    // This keeps non-finite framebuffer state fail-closed with respect to file
    // creation/truncation.
    const std::vector<std::uint8_t> payload = pfm_payload(resolved_color_, width_, height_);
    const std::string header = "PF\n" + std::to_string(width_) + ' '
        + std::to_string(height_) + "\n-1.0\n";

    validate_stream_write_size(header.size(), "PFM header size");
    validate_stream_write_size(payload.size(), "PFM payload size");

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open PFM output: " + path);
    }
    out.write(header.data(), static_cast<std::streamsize>(header.size()));
    out.write(
        reinterpret_cast<const char*>(payload.data()),
        static_cast<std::streamsize>(payload.size()));
    if (!out) {
        throw std::runtime_error("failed while writing PFM output: " + path);
    }
}

}  // namespace tiny_renderer
