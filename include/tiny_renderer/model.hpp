#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tiny_renderer/material.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

struct MaterialDraw {
    DrawRange range{};
    std::string material_name;
    MaterialState material{};
    std::shared_ptr<const Texture2D> diffuse_texture;
};

struct ModelAsset {
    Mesh mesh;
    std::vector<MaterialDraw> draws;
};

}  // namespace tiny_renderer
