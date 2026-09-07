#pragma once

#include <filesystem>

#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

// Load one bounded texture image selected by its filename extension.
// Supported formats are binary P6 PPM, uncompressed 24-bit true-color TGA,
// and RGB PFM. Transfer interpretation is explicit and defaults to the
// historical linear source behavior. PFM is intrinsically routed as linear
// float data and rejects an sRGB interpretation.
[[nodiscard]] Texture2D load_texture_image_file(
    const std::filesystem::path& path,
    TextureTransferFunction transfer_function = TextureTransferFunction::Linear);

}  // namespace tiny_renderer
