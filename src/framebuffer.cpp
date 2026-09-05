#include "tiny_renderer/framebuffer.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace tiny_renderer {

namespace {
std::uint8_t to_u8(float value) {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0F));
}

bool finite_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

Vec3 multiply_components(const Vec3& a, const Vec3& b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

Vec3 one_minus(const Vec3& value) {
    return {1.0F - value.x, 1.0F - value.y, 1.0F - value.z};
}

Vec3 replicated(float value) {
    return {value, value, value};
}

std::size_t checked_samples_per_pixel(SampleCount sample_count) {
    switch (sample_count) {
        case SampleCount::One:
            return 1U;
        case SampleCount::Four:
            return 4U;
    }
    throw std::invalid_argument("unsupported framebuffer sample count");
}

std::size_t checked_pixel_count(std::size_t width, std::size_t height) {
    if (width == 0U || height == 0U) {
        throw std::invalid_argument("framebuffer dimensions must be non-zero");
    }
    if (height > std::numeric_limits<std::size_t>::max() / width) {
        throw std::overflow_error("framebuffer pixel count overflows size_t");
    }
    return width * height;
}

bool depth_compare_passes(DepthCompare compare, float incoming, float stored) {
    switch (compare) {
        case DepthCompare::Less:
            return incoming < stored;
        case DepthCompare::LessEqual:
            return incoming <= stored;
        case DepthCompare::Greater:
            return incoming > stored;
        case DepthCompare::GreaterEqual:
            return incoming >= stored;
        case DepthCompare::Always:
            return true;
        case DepthCompare::Never:
            return false;
    }
    throw std::invalid_argument("unknown depth comparison mode");
}

bool stencil_compare_passes(
    StencilCompare compare,
    std::uint8_t reference,
    std::uint8_t stored,
    std::uint8_t read_mask) {
    const std::uint8_t masked_reference = static_cast<std::uint8_t>(reference & read_mask);
    const std::uint8_t masked_stored = static_cast<std::uint8_t>(stored & read_mask);
    switch (compare) {
        case StencilCompare::Never:
            return false;
        case StencilCompare::Less:
            return masked_reference < masked_stored;
        case StencilCompare::LessEqual:
            return masked_reference <= masked_stored;
        case StencilCompare::Greater:
            return masked_reference > masked_stored;
        case StencilCompare::GreaterEqual:
            return masked_reference >= masked_stored;
        case StencilCompare::Equal:
            return masked_reference == masked_stored;
        case StencilCompare::NotEqual:
            return masked_reference != masked_stored;
        case StencilCompare::Always:
            return true;
    }
    throw std::invalid_argument("unknown stencil comparison mode");
}

std::uint8_t stencil_operation_result(
    StencilOp operation,
    std::uint8_t stored,
    std::uint8_t reference) {
    switch (operation) {
        case StencilOp::Keep:
            return stored;
        case StencilOp::Zero:
            return 0U;
        case StencilOp::Replace:
            return reference;
        case StencilOp::IncrementClamp:
            return stored == std::numeric_limits<std::uint8_t>::max()
                ? stored
                : static_cast<std::uint8_t>(stored + 1U);
        case StencilOp::DecrementClamp:
            return stored == 0U ? stored : static_cast<std::uint8_t>(stored - 1U);
        case StencilOp::Invert:
            return static_cast<std::uint8_t>(~stored);
    }
    throw std::invalid_argument("unknown stencil operation");
}

void apply_stencil_operation(
    std::uint8_t& stored,
    StencilOp operation,
    std::uint8_t reference,
    std::uint8_t write_mask) {
    const std::uint8_t result = stencil_operation_result(operation, stored, reference);
    const std::uint8_t preserved = static_cast<std::uint8_t>(stored & static_cast<std::uint8_t>(~write_mask));
    const std::uint8_t written = static_cast<std::uint8_t>(result & write_mask);
    stored = static_cast<std::uint8_t>(preserved | written);
}

Vec3 blend_factor_value(
    BlendFactor factor,
    const Vec3& source,
    const Vec3& destination,
    const Vec3& constant_color,
    float source_alpha) {
    switch (factor) {
        case BlendFactor::Zero:
            return {0.0F, 0.0F, 0.0F};
        case BlendFactor::One:
            return {1.0F, 1.0F, 1.0F};
        case BlendFactor::SourceColor:
            return source;
        case BlendFactor::OneMinusSourceColor:
            return one_minus(source);
        case BlendFactor::DestinationColor:
            return destination;
        case BlendFactor::OneMinusDestinationColor:
            return one_minus(destination);
        case BlendFactor::ConstantColor:
            return constant_color;
        case BlendFactor::OneMinusConstantColor:
            return one_minus(constant_color);
        case BlendFactor::SourceAlpha:
            return replicated(source_alpha);
        case BlendFactor::OneMinusSourceAlpha:
            return replicated(1.0F - source_alpha);
    }
    throw std::invalid_argument("unknown RGB blend factor");
}

Vec3 blended_rgb(
    const Vec3& source,
    const Vec3& destination,
    const BlendState& state,
    float source_alpha) {
    if (!state.enabled) {
        return source;
    }

    // Min/max intentionally ignore source/destination factors. This is the
    // renderer's documented RGB teaching contract, not a hardware-API claim.
    if (state.operation == BlendOp::Min) {
        return {
            std::min(source.x, destination.x),
            std::min(source.y, destination.y),
            std::min(source.z, destination.z),
        };
    }
    if (state.operation == BlendOp::Max) {
        return {
            std::max(source.x, destination.x),
            std::max(source.y, destination.y),
            std::max(source.z, destination.z),
        };
    }

    const Vec3 source_term = multiply_components(
        source,
        blend_factor_value(
            state.source_factor,
            source,
            destination,
            state.constant_color,
            source_alpha));
    const Vec3 destination_term = multiply_components(
        destination,
        blend_factor_value(
            state.destination_factor,
            source,
            destination,
            state.constant_color,
            source_alpha));

    switch (state.operation) {
        case BlendOp::Add:
            return source_term + destination_term;
        case BlendOp::Subtract:
            return source_term - destination_term;
        case BlendOp::ReverseSubtract:
            return destination_term - source_term;
        case BlendOp::Min:
        case BlendOp::Max:
            break;
    }
    throw std::logic_error("unreachable RGB blend operation");
}

Vec3 apply_color_write_mask(
    const Vec3& resolved,
    const Vec3& destination,
    const ColorWriteMask& mask) {
    return {
        mask.red ? resolved.x : destination.x,
        mask.green ? resolved.y : destination.y,
        mask.blue ? resolved.z : destination.z,
    };
}
}  // namespace

void validate_depth_state(const DepthState& state) {
    switch (state.compare) {
        case DepthCompare::Less:
        case DepthCompare::LessEqual:
        case DepthCompare::Greater:
        case DepthCompare::GreaterEqual:
        case DepthCompare::Always:
        case DepthCompare::Never:
            return;
    }
    throw std::invalid_argument("unknown depth comparison mode");
}

void validate_stencil_state(const StencilState& state) {
    switch (state.compare) {
        case StencilCompare::Never:
        case StencilCompare::Less:
        case StencilCompare::LessEqual:
        case StencilCompare::Greater:
        case StencilCompare::GreaterEqual:
        case StencilCompare::Equal:
        case StencilCompare::NotEqual:
        case StencilCompare::Always:
            break;
        default:
            throw std::invalid_argument("unknown stencil comparison mode");
    }

    const auto validate_operation = [](StencilOp operation) {
        switch (operation) {
            case StencilOp::Keep:
            case StencilOp::Zero:
            case StencilOp::Replace:
            case StencilOp::IncrementClamp:
            case StencilOp::DecrementClamp:
            case StencilOp::Invert:
                return;
        }
        throw std::invalid_argument("unknown stencil operation");
    };

    validate_operation(state.stencil_fail);
    validate_operation(state.depth_fail);
    validate_operation(state.pass);
}

void validate_blend_state(const BlendState& state) {
    const auto validate_factor = [](BlendFactor factor) {
        switch (factor) {
            case BlendFactor::Zero:
            case BlendFactor::One:
            case BlendFactor::SourceColor:
            case BlendFactor::OneMinusSourceColor:
            case BlendFactor::DestinationColor:
            case BlendFactor::OneMinusDestinationColor:
            case BlendFactor::ConstantColor:
            case BlendFactor::OneMinusConstantColor:
            case BlendFactor::SourceAlpha:
            case BlendFactor::OneMinusSourceAlpha:
                return;
        }
        throw std::invalid_argument("unknown RGB blend factor");
    };
    validate_factor(state.source_factor);
    validate_factor(state.destination_factor);

    switch (state.operation) {
        case BlendOp::Add:
        case BlendOp::Subtract:
        case BlendOp::ReverseSubtract:
        case BlendOp::Min:
        case BlendOp::Max:
            break;
        default:
            throw std::invalid_argument("unknown RGB blend operation");
    }

    if (!finite_vec3(state.constant_color)
        || state.constant_color.x < 0.0F || state.constant_color.x > 1.0F
        || state.constant_color.y < 0.0F || state.constant_color.y > 1.0F
        || state.constant_color.z < 0.0F || state.constant_color.z > 1.0F) {
        throw std::invalid_argument("RGB blend constant components must be finite and within [0, 1]");
    }
}

Framebuffer::Framebuffer(
    std::size_t width,
    std::size_t height,
    SampleCount sample_count)
    : width_(width), height_(height), sample_count_(sample_count) {
    const std::size_t pixel_count = checked_pixel_count(width, height);
    const std::size_t sample_count_value = checked_samples_per_pixel(sample_count);
    if (pixel_count > std::numeric_limits<std::size_t>::max() / sample_count_value) {
        throw std::overflow_error("framebuffer sample storage size overflows size_t");
    }
    const std::size_t sample_storage_count = pixel_count * sample_count_value;

    resolved_color_.resize(pixel_count);
    sample_color_.resize(sample_storage_count);
    sample_depth_.resize(sample_storage_count);
    sample_stencil_.resize(sample_storage_count);
    clear();
}

void Framebuffer::clear(const Vec3& color, float depth, std::uint8_t stencil) {
    std::fill(resolved_color_.begin(), resolved_color_.end(), color);
    std::fill(sample_color_.begin(), sample_color_.end(), color);
    std::fill(sample_depth_.begin(), sample_depth_.end(), depth);
    std::fill(sample_stencil_.begin(), sample_stencil_.end(), stencil);
}

bool Framebuffer::test_and_write(
    std::size_t x,
    std::size_t y,
    float depth,
    const Vec3& color,
    DepthState depth_state,
    StencilState stencil_state,
    BlendState blend_state,
    float source_alpha) {
    if (sample_count_ != SampleCount::One) {
        throw std::invalid_argument(
            "pixel-level fragment ownership requires a single-sample framebuffer; use test_and_write_sample");
    }
    return test_and_write_sample(
        x,
        y,
        0U,
        depth,
        color,
        depth_state,
        stencil_state,
        blend_state,
        source_alpha);
}

bool Framebuffer::test_and_write_sample(
    std::size_t x,
    std::size_t y,
    std::size_t sample_index,
    float depth,
    const Vec3& color,
    DepthState depth_state,
    StencilState stencil_state,
    BlendState blend_state,
    float source_alpha) {
    validate_depth_state(depth_state);
    validate_stencil_state(stencil_state);
    validate_blend_state(blend_state);
    if (!std::isfinite(source_alpha) || source_alpha < 0.0F || source_alpha > 1.0F) {
        throw std::invalid_argument("fragment source alpha must be finite and within [0, 1]");
    }
    if (sample_index >= samples_per_pixel()) {
        throw std::out_of_range("framebuffer sample index out of range");
    }
    if (x >= width_ || y >= height_ || !std::isfinite(depth)) {
        return false;
    }

    const std::size_t pixel = pixel_index(x, y);
    const std::size_t sample = pixel * samples_per_pixel() + sample_index;
    if (stencil_state.enabled
        && !stencil_compare_passes(
            stencil_state.compare,
            stencil_state.reference,
            sample_stencil_[sample],
            stencil_state.read_mask)) {
        apply_stencil_operation(
            sample_stencil_[sample],
            stencil_state.stencil_fail,
            stencil_state.reference,
            stencil_state.write_mask);
        return false;
    }

    if (!depth_compare_passes(depth_state.compare, depth, sample_depth_[sample])) {
        if (stencil_state.enabled) {
            apply_stencil_operation(
                sample_stencil_[sample],
                stencil_state.depth_fail,
                stencil_state.reference,
                stencil_state.write_mask);
        }
        return false;
    }

    const Vec3 destination = sample_color_[sample];
    const Vec3 resolved_sample_color = apply_color_write_mask(
        blended_rgb(color, destination, blend_state, source_alpha),
        destination,
        blend_state.write_mask);

    if (stencil_state.enabled) {
        apply_stencil_operation(
            sample_stencil_[sample],
            stencil_state.pass,
            stencil_state.reference,
            stencil_state.write_mask);
    }
    if (depth_state.write_enabled) {
        sample_depth_[sample] = depth;
    }
    sample_color_[sample] = resolved_sample_color;
    resolve_pixel(pixel);
    return true;
}

bool Framebuffer::depth_test_and_write(
    std::size_t x,
    std::size_t y,
    float depth,
    const Vec3& color,
    DepthState state) {
    return test_and_write(x, y, depth, color, state, {}, {}, 1.0F);
}

const Vec3& Framebuffer::color_at(std::size_t x, std::size_t y) const {
    return resolved_color_.at(pixel_index(x, y));
}

float Framebuffer::depth_at(std::size_t x, std::size_t y) const {
    return sample_depth_at(x, y, 0U);
}

std::uint8_t Framebuffer::stencil_at(std::size_t x, std::size_t y) const {
    return sample_stencil_at(x, y, 0U);
}

const Vec3& Framebuffer::sample_color_at(
    std::size_t x,
    std::size_t y,
    std::size_t sample_index) const {
    return sample_color_.at(sample_storage_index(x, y, sample_index));
}

float Framebuffer::sample_depth_at(
    std::size_t x,
    std::size_t y,
    std::size_t sample_index) const {
    return sample_depth_.at(sample_storage_index(x, y, sample_index));
}

std::uint8_t Framebuffer::sample_stencil_at(
    std::size_t x,
    std::size_t y,
    std::size_t sample_index) const {
    return sample_stencil_.at(sample_storage_index(x, y, sample_index));
}

std::vector<std::uint8_t> Framebuffer::rgb8() const {
    if (resolved_color_.size() > std::numeric_limits<std::size_t>::max() / 3U) {
        throw std::overflow_error("RGB8 output size overflows size_t");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(resolved_color_.size() * 3U);
    for (const Vec3& pixel : resolved_color_) {
        bytes.push_back(to_u8(pixel.x));
        bytes.push_back(to_u8(pixel.y));
        bytes.push_back(to_u8(pixel.z));
    }
    return bytes;
}

std::uint64_t Framebuffer::fnv1a64() const {
    constexpr std::uint64_t offset = 14695981039346656037ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    for (const std::uint8_t byte : rgb8()) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= prime;
    }
    return hash;
}

void Framebuffer::write_ppm(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to open PPM output: " + path);
    }
    out << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    const std::vector<std::uint8_t> bytes = rgb8();
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        throw std::runtime_error("failed while writing PPM output: " + path);
    }
}

std::size_t Framebuffer::pixel_index(std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("framebuffer coordinate out of range");
    }
    return y * width_ + x;
}

std::size_t Framebuffer::sample_storage_index(
    std::size_t x,
    std::size_t y,
    std::size_t sample_index) const {
    if (sample_index >= samples_per_pixel()) {
        throw std::out_of_range("framebuffer sample index out of range");
    }
    return pixel_index(x, y) * samples_per_pixel() + sample_index;
}

void Framebuffer::resolve_pixel(std::size_t pixel) {
    const std::size_t count = samples_per_pixel();
    const std::size_t first = pixel * count;
    if (count == 1U) {
        resolved_color_[pixel] = sample_color_[first];
        return;
    }

    Vec3 sum{0.0F, 0.0F, 0.0F};
    for (std::size_t sample = 0U; sample < count; ++sample) {
        sum = sum + sample_color_[first + sample];
    }
    resolved_color_[pixel] = sum * (1.0F / static_cast<float>(count));
}

}  // namespace tiny_renderer
