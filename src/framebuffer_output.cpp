#include "tiny_renderer/framebuffer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace tiny_renderer {
namespace {

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

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open PPM output: " + path);
    }
    out << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    const std::vector<std::uint8_t> bytes = rgb8(transfer_function);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw std::runtime_error("failed while writing PPM output: " + path);
    }
}

}  // namespace tiny_renderer
