#pragma once

#include <cmath>
#include <cstddef>
#include <stdexcept>

#include "tiny_renderer/rasterizer.hpp"

namespace tiny_renderer::detail {

struct TangentFrame {
    Vec3 tangent;
    Vec3 bitangent;
};

inline bool normal_mapping_finite_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline Mat3 model_linear_matrix(const Mat4& model) {
    Mat3 result({
        model(0, 0), model(0, 1), model(0, 2),
        model(1, 0), model(1, 1), model(1, 2),
        model(2, 0), model(2, 1), model(2, 2),
    });
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            if (!std::isfinite(result(row, column))) {
                throw std::invalid_argument("normal mapping model linear transform must be finite");
            }
        }
    }
    return result;
}

inline void validate_normal_texture(const Texture2D& texture) {
    if (!texture.texels_within_unit_range()) {
        throw std::invalid_argument(
            "normal map texels must be finite and within [0, 1]");
    }
}

inline TangentFrame derive_object_tangent_frame(
    const Triangle& triangle,
    const TextureBinding& binding) {
    const Vec2 uv0{
        triangle[0].varyings.values[binding.u_channel],
        triangle[0].varyings.values[binding.v_channel],
    };
    const Vec2 uv1{
        triangle[1].varyings.values[binding.u_channel],
        triangle[1].varyings.values[binding.v_channel],
    };
    const Vec2 uv2{
        triangle[2].varyings.values[binding.u_channel],
        triangle[2].varyings.values[binding.v_channel],
    };
    const Vec3 edge1 = triangle[1].position - triangle[0].position;
    const Vec3 edge2 = triangle[2].position - triangle[0].position;
    const Vec3 geometric_cross = cross(edge1, edge2);
    if (!normal_mapping_finite_vec3(geometric_cross)
        || length(geometric_cross) <= kEpsilon) {
        throw std::invalid_argument(
            "normal mapping requires a non-degenerate triangle geometry basis");
    }
    const Vec2 delta_uv1 = uv1 - uv0;
    const Vec2 delta_uv2 = uv2 - uv0;
    const float determinant = delta_uv1.x * delta_uv2.y - delta_uv1.y * delta_uv2.x;
    if (!std::isfinite(determinant) || std::fabs(determinant) <= kEpsilon) {
        throw std::invalid_argument(
            "normal mapping requires a non-degenerate triangle UV basis");
    }
    const float inverse = 1.0F / determinant;
    const Vec3 tangent =
        (edge1 * delta_uv2.y - edge2 * delta_uv1.y) * inverse;
    const Vec3 bitangent =
        (edge2 * delta_uv1.x - edge1 * delta_uv2.x) * inverse;
    const Vec3 frame_cross = cross(tangent, bitangent);
    if (!normal_mapping_finite_vec3(tangent)
        || !normal_mapping_finite_vec3(bitangent)
        || !normal_mapping_finite_vec3(frame_cross)
        || length(tangent) <= kEpsilon
        || length(bitangent) <= kEpsilon
        || length(frame_cross) <= kEpsilon) {
        throw std::invalid_argument(
            "normal mapping requires a stable object-space tangent frame");
    }
    return {tangent, bitangent};
}

inline TangentFrame transform_tangent_frame(
    const TangentFrame& object_frame,
    const Mat4& model) {
    const Mat3 linear = model_linear_matrix(model);
    const Vec3 tangent = linear * object_frame.tangent;
    const Vec3 bitangent = linear * object_frame.bitangent;
    const Vec3 frame_cross = cross(tangent, bitangent);
    if (!normal_mapping_finite_vec3(tangent)
        || !normal_mapping_finite_vec3(bitangent)
        || !normal_mapping_finite_vec3(frame_cross)
        || length(tangent) <= kEpsilon
        || length(bitangent) <= kEpsilon
        || length(frame_cross) <= kEpsilon) {
        throw std::invalid_argument(
            "normal mapping model transform collapses the tangent frame");
    }
    return {tangent, bitangent};
}

inline TangentFrame prepare_tangent_frame(
    const Triangle& triangle,
    const TextureBinding& binding,
    const Mat4& model) {
    if (binding.normal_texture == nullptr) {
        throw std::logic_error("normal mapping tangent frame requested without a normal texture");
    }
    validate_normal_texture(*binding.normal_texture);
    return transform_tangent_frame(
        derive_object_tangent_frame(triangle, binding),
        model);
}

inline Vec3 tangent_space_lighting_normal(
    const TextureBinding& binding,
    const VaryingPack& varyings,
    const TextureGradients& gradients,
    const Vec3& geometric_normal,
    const TangentFrame* frame) {
    if (binding.normal_texture == nullptr) {
        return geometric_normal;
    }
    if (frame == nullptr) {
        throw std::logic_error("normal mapping shading lost its tangent frame");
    }

    const Vec3 sampled = binding.normal_texture->sample_grad(
        {varyings.values[binding.u_channel], varyings.values[binding.v_channel]},
        gradients,
        binding.sampler);
    const Vec3 tangent_normal{
        sampled.x * 2.0F - 1.0F,
        sampled.y * 2.0F - 1.0F,
        sampled.z * 2.0F - 1.0F,
    };
    const float tangent_normal_length = length(tangent_normal);
    if (!normal_mapping_finite_vec3(tangent_normal)
        || !std::isfinite(tangent_normal_length)
        || tangent_normal_length <= kEpsilon) {
        return geometric_normal;
    }
    const Vec3 mapped = tangent_normal / tangent_normal_length;

    Vec3 tangent_projected =
        frame->tangent - geometric_normal * dot(geometric_normal, frame->tangent);
    const float tangent_length = length(tangent_projected);
    Vec3 tangent{};
    Vec3 bitangent{};
    if (normal_mapping_finite_vec3(tangent_projected)
        && std::isfinite(tangent_length)
        && tangent_length > kEpsilon) {
        tangent = tangent_projected / tangent_length;
        Vec3 derived_bitangent = cross(geometric_normal, tangent);
        const float derived_length = length(derived_bitangent);
        if (!normal_mapping_finite_vec3(derived_bitangent)
            || !std::isfinite(derived_length)
            || derived_length <= kEpsilon) {
            return geometric_normal;
        }
        derived_bitangent = derived_bitangent / derived_length;
        if (dot(derived_bitangent, frame->bitangent) < 0.0F) {
            derived_bitangent = derived_bitangent * -1.0F;
        }
        bitangent = derived_bitangent;
    } else {
        Vec3 bitangent_projected =
            frame->bitangent - geometric_normal * dot(geometric_normal, frame->bitangent);
        const float bitangent_length = length(bitangent_projected);
        if (!normal_mapping_finite_vec3(bitangent_projected)
            || !std::isfinite(bitangent_length)
            || bitangent_length <= kEpsilon) {
            return geometric_normal;
        }
        bitangent = bitangent_projected / bitangent_length;
        tangent = cross(bitangent, geometric_normal);
        const float derived_length = length(tangent);
        if (!normal_mapping_finite_vec3(tangent)
            || !std::isfinite(derived_length)
            || derived_length <= kEpsilon) {
            return geometric_normal;
        }
        tangent = tangent / derived_length;
    }

    const Vec3 world = tangent * mapped.x
        + bitangent * mapped.y
        + geometric_normal * mapped.z;
    const float world_length = length(world);
    if (!normal_mapping_finite_vec3(world)
        || !std::isfinite(world_length)
        || world_length <= kEpsilon) {
        return geometric_normal;
    }
    return world / world_length;
}

}  // namespace tiny_renderer::detail
