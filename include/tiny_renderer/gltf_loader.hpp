#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/model.hpp"
#include "tiny_renderer/skinning.hpp"
#include "tiny_renderer/skeletal_trs_timeline.hpp"
#include "tiny_renderer/skeletal_trs_timeline.hpp"

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
    std::shared_ptr<const SkeletalTrsClip> linear_animation{};
};

[[nodiscard]] GltfSkinnedAsset load_gltf_skinned_asset_file(
    const std::filesystem::path& path);

struct GltfSkinnedAnimatedAsset {
    GltfSkinnedAsset asset{};
    std::shared_ptr<const SkeletalTrsClip> animation{};
};

[[nodiscard]] GltfSkinnedAnimatedAsset
load_gltf_skinned_animated_asset_file(
    const std::filesystem::path& path);

}  // namespace tiny_renderer
