#include "tiny_renderer/obj_loader.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/mtl_loader.hpp"
#include "tiny_renderer/ppm_loader.hpp"

#include "material_library_resolver.hpp"

namespace tiny_renderer {
namespace {

[[noreturn]] void fail(std::size_t line, const std::string& message) {
    throw ObjParseError(line, message);
}

std::shared_ptr<const Texture2D> load_owned_texture(
    const std::filesystem::path& library_directory,
    const std::optional<std::string>& filename,
    std::map<std::string, std::shared_ptr<const Texture2D>>& texture_cache) {
    if (!filename) {
        return {};
    }

    const std::filesystem::path texture_path =
        (library_directory / *filename).lexically_normal();
    const std::string cache_key = texture_path.string();
    const auto existing = texture_cache.find(cache_key);
    if (existing != texture_cache.end()) {
        return existing->second;
    }

    auto texture = std::make_shared<const Texture2D>(load_ppm_file(texture_path));
    texture_cache.emplace(cache_key, texture);
    return texture;
}

void validate_generated_range(const Mesh& mesh, DrawRange range) {
    if (range.first_triangle > mesh.triangles.size()
        || range.triangle_count > mesh.triangles.size() - range.first_triangle) {
        throw std::logic_error("generated material draw range exceeds canonical mesh");
    }
}

struct ResolvedMaterialAssetDefinition {
    MaterialAssetDefinition definition;
    std::filesystem::path library_directory;
};

struct LoadedMaterialAssetDefinition {
    MaterialState material{};
    std::shared_ptr<const Texture2D> diffuse_texture;
    std::shared_ptr<const Texture2D> opacity_texture;
    std::shared_ptr<const Texture2D> normal_texture;
};

}  // namespace

ModelAsset load_obj_model_asset_file(const std::filesystem::path& path) {
    ObjModelSource source = load_obj_model_source_file(path);
    ModelAsset asset;
    asset.mesh = std::move(source.mesh);

    if (source.material_libraries.empty()) {
        if (!asset.mesh.triangles.empty()) {
            MaterialDraw draw;
            draw.range = {0U, asset.mesh.triangles.size()};
            asset.draws.push_back(std::move(draw));
        }
        return asset;
    }

    const auto library =
        detail::load_obj_material_library_set<ResolvedMaterialAssetDefinition>(
            path,
            source.material_libraries,
            [](const std::filesystem::path& library_path) {
                return load_mtl_assets_file(library_path);
            },
            [](MaterialAssetDefinition definition, const std::filesystem::path& library_path) {
                return ResolvedMaterialAssetDefinition{
                    std::move(definition),
                    library_path.parent_path(),
                };
            });
    for (const ObjMaterialUse& used : source.used_materials) {
        if (library.find(used.name) == library.end()) {
            fail(used.line, "usemtl references unknown material '" + used.name + "'");
        }
    }

    std::map<std::string, std::shared_ptr<const Texture2D>> texture_cache;
    std::map<std::string, LoadedMaterialAssetDefinition> materials;
    for (const auto& [name, resolved] : library) {
        LoadedMaterialAssetDefinition loaded;
        loaded.material = resolved.definition.material;
        loaded.diffuse_texture = load_owned_texture(
            resolved.library_directory,
            resolved.definition.diffuse_map_filename,
            texture_cache);
        loaded.opacity_texture = load_owned_texture(
            resolved.library_directory,
            resolved.definition.opacity_map_filename,
            texture_cache);
        loaded.normal_texture = load_owned_texture(
            resolved.library_directory,
            resolved.definition.normal_map_filename,
            texture_cache);
        materials.emplace(name, std::move(loaded));
    }

    for (std::size_t face = 0U; face < asset.mesh.triangles.size(); ++face) {
        const std::string& material_name = source.face_materials[face];
        const LoadedMaterialAssetDefinition& definition = materials.at(material_name);
        if (asset.draws.empty() || asset.draws.back().material_name != material_name) {
            MaterialDraw draw;
            draw.range.first_triangle = face;
            draw.material_name = material_name;
            draw.material = definition.material;
            draw.diffuse_texture = definition.diffuse_texture;
            draw.opacity_texture = definition.opacity_texture;
            draw.normal_texture = definition.normal_texture;
            asset.draws.push_back(std::move(draw));
        }
        ++asset.draws.back().range.triangle_count;
    }

    return asset;
}

std::vector<MaterialAssetBatch> load_obj_material_asset_batches_file(const std::filesystem::path& path) {
    const ModelAsset asset = load_obj_model_asset_file(path);
    std::vector<MaterialAssetBatch> batches;
    batches.reserve(asset.draws.size());

    for (const MaterialDraw& draw : asset.draws) {
        validate_generated_range(asset.mesh, draw.range);
        MaterialAssetBatch batch;
        batch.mesh.vertices = asset.mesh.vertices;
        using Difference = std::vector<TriangleIndices>::difference_type;
        const auto first = asset.mesh.triangles.begin() + static_cast<Difference>(draw.range.first_triangle);
        const auto last = first + static_cast<Difference>(draw.range.triangle_count);
        batch.mesh.triangles.assign(first, last);
        batch.material_name = draw.material_name;
        batch.material = draw.material;
        batch.diffuse_texture = draw.diffuse_texture;
        batch.opacity_texture = draw.opacity_texture;
        batch.normal_texture = draw.normal_texture;
        batches.push_back(std::move(batch));
    }

    return batches;
}

}  // namespace tiny_renderer
