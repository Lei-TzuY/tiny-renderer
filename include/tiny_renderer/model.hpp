#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tiny_renderer/material.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

struct VertexColorChannels {
    std::size_t red{};
    std::size_t green{};
    std::size_t blue{};

    friend bool operator==(const VertexColorChannels&, const VertexColorChannels&) = default;
};

struct MaterialDraw {
    DrawRange range{};
    std::string material_name;
    MaterialState material{};
    std::shared_ptr<const Texture2D> diffuse_texture;
    std::shared_ptr<const Texture2D> opacity_texture;
    std::shared_ptr<const Texture2D> normal_texture;
    // Optional per-fragment specular-reflectance multiplier. It shares the
    // material UV/sampler path and owned texture lifetime with other roles.
    std::shared_ptr<const Texture2D> specular_texture;
    // Optional linear/HDR emissive-radiance multiplier sharing the
    // canonical material UV/sampler path.
    std::shared_ptr<const Texture2D> emissive_texture;
    // Optional linear data map resolved to one per-fragment shininess
    // value shared by direct and environment specular consumers.
    std::shared_ptr<const Texture2D> shininess_texture;
};

struct ModelAsset {
    Mesh mesh;
    std::vector<MaterialDraw> draws;
    // Optional semantic binding for canonical per-vertex RGB imported with the
    // geometry. The color values remain ordinary smooth varying channels; this
    // metadata only tells model submission when those channels are the intended
    // base-color source for an untextured draw.
    std::optional<VertexColorChannels> vertex_color_channels{};
};

}  // namespace tiny_renderer
