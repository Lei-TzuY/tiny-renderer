#include "tiny_renderer/obj_loader.hpp"

#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/mtl_loader.hpp"
#include "tiny_renderer/ppm_loader.hpp"

namespace tiny_renderer {
namespace {

struct UsedMaterial {
    std::string name;
    std::size_t line{};
};

struct AssetMaterialMetadata {
    std::optional<std::string> library_filename;
    std::vector<UsedMaterial> used_materials;
    std::vector<std::string> face_materials;
};

[[noreturn]] void fail(std::size_t line, const std::string& message) {
    throw ObjParseError(line, message);
}

void validate_sibling_library_filename(const std::string& token, std::size_t line) {
    const std::filesystem::path path(token);
    if (token.empty() || path.is_absolute() || path.has_parent_path()
        || token.find('/') != std::string::npos || token.find('\\') != std::string::npos
        || token == "." || token == "..") {
        fail(line, "mtllib must name exactly one sibling material file");
    }
}

AssetMaterialMetadata scan_asset_material_metadata(std::istream& input) {
    AssetMaterialMetadata metadata;
    std::optional<std::string> active_material;
    bool saw_face = false;

    std::string line_text;
    std::size_t line_number = 0U;
    while (std::getline(input, line_text)) {
        ++line_number;
        const std::size_t comment = line_text.find('#');
        if (comment != std::string::npos) {
            line_text.erase(comment);
        }

        std::istringstream line(line_text);
        std::string directive;
        if (!(line >> directive)) {
            continue;
        }

        if (directive == "mtllib") {
            std::string filename;
            std::string extra;
            if (!(line >> filename) || (line >> extra)) {
                fail(line_number, "mtllib must contain exactly one filename");
            }
            if (saw_face) {
                fail(line_number, "mtllib must appear before material-bound faces");
            }
            if (metadata.library_filename) {
                fail(line_number, "only one mtllib directive is supported");
            }
            validate_sibling_library_filename(filename, line_number);
            metadata.library_filename = filename;
            continue;
        }

        if (directive == "usemtl") {
            std::string name;
            std::string extra;
            if (!(line >> name) || (line >> extra)) {
                fail(line_number, "usemtl must contain exactly one material name");
            }
            if (!metadata.library_filename) {
                fail(line_number, "usemtl requires a preceding mtllib directive");
            }
            active_material = name;
            metadata.used_materials.push_back({name, line_number});
            continue;
        }

        if (directive == "f") {
            saw_face = true;
            if (metadata.library_filename && !active_material) {
                fail(line_number, "material-aware OBJ faces require an active usemtl material");
            }
            metadata.face_materials.push_back(active_material.value_or(std::string{}));
        }
    }

    if (input.bad()) {
        throw std::runtime_error("failed while reading OBJ material metadata");
    }
    return metadata;
}

std::shared_ptr<const Texture2D> load_owned_diffuse_texture(
    const std::filesystem::path& library_directory,
    const MaterialAssetDefinition& definition,
    std::map<std::string, std::shared_ptr<const Texture2D>>& texture_cache) {
    if (!definition.diffuse_map_filename) {
        return {};
    }

    const std::filesystem::path texture_path =
        (library_directory / *definition.diffuse_map_filename).lexically_normal();
    const std::string cache_key = texture_path.string();
    const auto existing = texture_cache.find(cache_key);
    if (existing != texture_cache.end()) {
        return existing->second;
    }

    auto texture = std::make_shared<const Texture2D>(load_ppm_file(texture_path));
    texture_cache.emplace(cache_key, texture);
    return texture;
}

}  // namespace

std::vector<MaterialAssetBatch> load_obj_material_asset_batches_file(const std::filesystem::path& path) {
    const Mesh geometry = load_obj_file(path);

    std::ifstream metadata_input(path);
    if (!metadata_input) {
        throw std::runtime_error("failed to reopen OBJ file for material metadata: " + path.string());
    }
    const AssetMaterialMetadata metadata = scan_asset_material_metadata(metadata_input);
    if (metadata.face_materials.size() != geometry.triangles.size()) {
        throw std::runtime_error("OBJ geometry/material face count changed between parsing passes");
    }

    if (!metadata.library_filename) {
        if (geometry.triangles.empty()) {
            return {};
        }
        return {MaterialAssetBatch{geometry, std::string{}, MaterialState{}, {}}};
    }

    const std::filesystem::path library_path = path.parent_path() / *metadata.library_filename;
    const MaterialAssetLibrary library = load_mtl_assets_file(library_path);
    for (const UsedMaterial& used : metadata.used_materials) {
        if (library.find(used.name) == library.end()) {
            fail(used.line, "usemtl references unknown material '" + used.name + "'");
        }
    }

    std::map<std::string, std::shared_ptr<const Texture2D>> texture_cache;
    std::map<std::string, std::shared_ptr<const Texture2D>> material_textures;
    for (const auto& [name, definition] : library) {
        material_textures.emplace(
            name,
            load_owned_diffuse_texture(library_path.parent_path(), definition, texture_cache));
    }

    std::vector<MaterialAssetBatch> batches;
    for (std::size_t face = 0U; face < geometry.triangles.size(); ++face) {
        const std::string& material_name = metadata.face_materials[face];
        const MaterialAssetDefinition& definition = library.at(material_name);
        if (batches.empty() || batches.back().material_name != material_name) {
            MaterialAssetBatch batch;
            batch.mesh.vertices = geometry.vertices;
            batch.material_name = material_name;
            batch.material = definition.material;
            batch.diffuse_texture = material_textures.at(material_name);
            batches.push_back(std::move(batch));
        }
        batches.back().mesh.triangles.push_back(geometry.triangles[face]);
    }
    return batches;
}

}  // namespace tiny_renderer
