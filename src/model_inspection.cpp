#include "tiny_renderer/model_inspection.hpp"

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>

#include "tiny_renderer/model_fingerprint.hpp"

namespace tiny_renderer {
namespace {

void append_texture_summary(
    std::ostringstream& output,
    const char* field,
    const std::shared_ptr<const Texture2D>& texture) {
    output << field << '=';
    if (!texture) {
        output << "none\n";
        return;
    }
    output << texture->width() << 'x' << texture->height() << '\n';
}

}  // namespace

std::string inspect_model_asset(const ModelAsset& asset) {
    std::ostringstream output;
    output << "format=tiny-renderer-model-asset-inspect-v1\n";
    output << "fingerprint_fnv1a64="
           << std::hex << std::setfill('0') << std::setw(16)
           << model_asset_fnv1a64(asset) << std::dec << '\n';
    output << "vertices=" << asset.mesh.vertices.size() << '\n';
    output << "triangles=" << asset.mesh.triangles.size() << '\n';
    output << "draws=" << asset.draws.size() << '\n';

    for (std::size_t index = 0U; index < asset.draws.size(); ++index) {
        const MaterialDraw& draw = asset.draws[index];
        output << "draw[" << index << "].first_triangle=" << draw.range.first_triangle << '\n';
        output << "draw[" << index << "].triangle_count=" << draw.range.triangle_count << '\n';
        output << "draw[" << index << "].material=" << draw.material_name << '\n';
        const std::string prefix = "draw[" + std::to_string(index) + "].";
        append_texture_summary(output, (prefix + "diffuse_texture").c_str(), draw.diffuse_texture);
        append_texture_summary(output, (prefix + "opacity_texture").c_str(), draw.opacity_texture);
        append_texture_summary(output, (prefix + "normal_texture").c_str(), draw.normal_texture);
    }

    return output.str();
}

}  // namespace tiny_renderer
