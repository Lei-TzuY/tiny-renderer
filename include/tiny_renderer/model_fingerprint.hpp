#pragma once

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

#include "tiny_renderer/model.hpp"

namespace tiny_renderer {
namespace detail {

constexpr std::uint64_t kModelFingerprintFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kModelFingerprintFnvPrime = 1099511628211ULL;

inline void model_fingerprint_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= static_cast<std::uint64_t>(value);
    hash *= kModelFingerprintFnvPrime;
}

inline void model_fingerprint_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) {
        model_fingerprint_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

inline void model_fingerprint_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0U; shift < 32U; shift += 8U) {
        model_fingerprint_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

inline void model_fingerprint_size(std::uint64_t& hash, std::size_t value) noexcept {
    static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
    model_fingerprint_u64(hash, static_cast<std::uint64_t>(value));
}

inline void model_fingerprint_float(std::uint64_t& hash, float value) noexcept {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    static_assert(std::numeric_limits<float>::is_iec559);

    std::uint32_t bits{};
    if (std::isnan(value)) {
        // Invalid model state can still be inspected reproducibly without
        // preserving platform-specific NaN payloads.
        bits = 0x7FC00000U;
    } else {
        // Signed zero is semantically identical throughout the renderer, so
        // normalize it for a logical-content fingerprint.
        const float canonical = value == 0.0F ? 0.0F : value;
        bits = std::bit_cast<std::uint32_t>(canonical);
    }
    model_fingerprint_u32(hash, bits);
}

inline void model_fingerprint_string(std::uint64_t& hash, std::string_view value) noexcept {
    model_fingerprint_size(hash, value.size());
    for (const char character : value) {
        model_fingerprint_byte(
            hash,
            static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
    }
}

inline void model_fingerprint_vec3(std::uint64_t& hash, const Vec3& value) noexcept {
    model_fingerprint_float(hash, value.x);
    model_fingerprint_float(hash, value.y);
    model_fingerprint_float(hash, value.z);
}

inline void model_fingerprint_texture(
    std::uint64_t& hash,
    const std::shared_ptr<const Texture2D>& texture) {
    model_fingerprint_byte(hash, static_cast<std::uint8_t>(texture ? 1U : 0U));
    if (!texture) {
        return;
    }

    model_fingerprint_size(hash, texture->width());
    model_fingerprint_size(hash, texture->height());
    for (std::size_t y = 0U; y < texture->height(); ++y) {
        for (std::size_t x = 0U; x < texture->width(); ++x) {
            model_fingerprint_vec3(hash, texture->texel(x, y));
        }
    }
}

}  // namespace detail

// Stable, versioned, non-cryptographic content fingerprint for the logical
// ModelAsset representation. Pointer identity and shared_ptr ownership topology
// are intentionally excluded: equal canonical asset content hashes equally even
// when textures are deep-copied into different allocation objects.
//
// The explicit byte order and float canonicalization make the result independent
// of host endianness for the renderer's required IEEE-754 binary32 float model.
[[nodiscard]] inline std::uint64_t model_asset_fnv1a64(const ModelAsset& asset) {
    std::uint64_t hash = detail::kModelFingerprintFnvOffset;
    detail::model_fingerprint_string(hash, "tiny-renderer:model-asset-fingerprint:v1");

    detail::model_fingerprint_size(hash, asset.mesh.vertices.size());
    for (const Vertex& vertex : asset.mesh.vertices) {
        detail::model_fingerprint_vec3(hash, vertex.position);
        detail::model_fingerprint_size(hash, vertex.varyings.count);
        for (std::size_t channel = 0U; channel < vertex.varyings.count; ++channel) {
            detail::model_fingerprint_u64(
                hash,
                static_cast<std::uint64_t>(vertex.varyings.interpolation[channel]));
            detail::model_fingerprint_float(hash, vertex.varyings.values[channel]);
        }
    }

    detail::model_fingerprint_size(hash, asset.mesh.triangles.size());
    for (const TriangleIndices& triangle : asset.mesh.triangles) {
        for (const std::uint32_t index : triangle) {
            detail::model_fingerprint_u32(hash, index);
        }
    }

    detail::model_fingerprint_size(hash, asset.draws.size());
    for (const MaterialDraw& draw : asset.draws) {
        detail::model_fingerprint_size(hash, draw.range.first_triangle);
        detail::model_fingerprint_size(hash, draw.range.triangle_count);
        detail::model_fingerprint_string(hash, draw.material_name);
        detail::model_fingerprint_vec3(hash, draw.material.albedo);
        detail::model_fingerprint_float(hash, draw.material.opacity);
        detail::model_fingerprint_vec3(hash, draw.material.specular);
        detail::model_fingerprint_float(hash, draw.material.shininess);
        detail::model_fingerprint_texture(hash, draw.diffuse_texture);
        detail::model_fingerprint_texture(hash, draw.opacity_texture);
        detail::model_fingerprint_texture(hash, draw.normal_texture);
    }

    return hash;
}

}  // namespace tiny_renderer
