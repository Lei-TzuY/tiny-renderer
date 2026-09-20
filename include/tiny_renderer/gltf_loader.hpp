#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "tiny_renderer/model.hpp"
#include "tiny_renderer/skinning.hpp"

namespace tiny_renderer {

class GltfLoadError : public std::runtime_error {
public:
    explicit GltfLoadError(const std::string& message)
        : std::runtime_error(message) {}
};

// Bounded glTF 2.0 skinned-asset projection. The imported mesh is the existing
// ModelAsset representation; normals, when present, occupy the returned smooth
// varying channels. The rig and rest local state use the exact M108 ownership
// and hierarchy semantics.
struct GltfSkinnedAsset {
    ModelAsset model{};
    SkeletalRigPtr rig{};
    std::vector<Mat4> rest_local_transforms{};
    std::optional<std::array<std::size_t, 3>> normal_channels{};
};

[[nodiscard]] GltfSkinnedAsset load_gltf_skinned_asset_file(
    const std::filesystem::path& path);

}  // namespace tiny_renderer
