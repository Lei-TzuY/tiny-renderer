#pragma once

#include <cstddef>
#include <filesystem>
#include <istream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/material.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/model.hpp"
#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

class ObjParseError : public std::runtime_error {
public:
    ObjParseError(std::size_t line, const std::string& message);

    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_{};
};

struct ObjMaterialUse {
    std::string name;
    std::size_t line{};
};

struct ObjModelSource {
    Mesh mesh;
    std::optional<std::string> material_library_filename;
    std::vector<ObjMaterialUse> used_materials;
    std::vector<std::string> face_materials;
};

struct MaterialBatch {
    Mesh mesh;
    std::string material_name;
    MaterialState material{};
};

struct MaterialAssetBatch {
    Mesh mesh;
    std::string material_name;
    MaterialState material{};
    std::shared_ptr<const Texture2D> diffuse_texture;
    std::shared_ptr<const Texture2D> opacity_texture;
};

[[nodiscard]] Mesh load_obj(std::istream& input);
[[nodiscard]] Mesh load_obj_file(const std::filesystem::path& path);
[[nodiscard]] ObjModelSource load_obj_model_source(std::istream& input);
[[nodiscard]] ObjModelSource load_obj_model_source_file(const std::filesystem::path& path);
[[nodiscard]] std::vector<MaterialBatch> load_obj_material_batches_file(const std::filesystem::path& path);
[[nodiscard]] std::vector<MaterialAssetBatch> load_obj_material_asset_batches_file(const std::filesystem::path& path);
[[nodiscard]] ModelAsset load_obj_model_asset_file(const std::filesystem::path& path);

}  // namespace tiny_renderer
