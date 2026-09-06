#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "tiny_renderer/obj_loader.hpp"

namespace tiny_renderer::detail {

template <typename OutputValue, typename Loader, typename Project>
std::map<std::string, OutputValue> load_obj_material_library_set(
    const std::filesystem::path& obj_path,
    const std::vector<ObjMaterialLibraryRef>& references,
    Loader&& loader,
    Project&& project) {
    std::map<std::string, OutputValue> result;
    for (const ObjMaterialLibraryRef& reference : references) {
        const std::filesystem::path library_path =
            (obj_path.parent_path() / reference.filename).lexically_normal();
        auto library = loader(library_path);
        for (auto& [name, value] : library) {
            if (result.find(name) != result.end()) {
                throw ObjParseError(
                    reference.line,
                    "material '" + name + "' is defined by multiple mtllib libraries");
            }
            result.emplace(name, project(std::move(value), library_path));
        }
    }
    return result;
}

}  // namespace tiny_renderer::detail
