#pragma once

#include <filesystem>
#include <istream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

class PfmParseError : public std::runtime_error {
public:
    explicit PfmParseError(const std::string& message) : std::runtime_error(message) {}
};

// Load bounded RGB PFM data directly into the renderer's linear-float texture
// domain. This first slice accepts only canonical unit-magnitude endian markers:
// -1.0 for little-endian payloads and 1.0 for big-endian payloads.
[[nodiscard]] Texture2D load_pfm(std::istream& input);
[[nodiscard]] Texture2D load_pfm_file(const std::filesystem::path& path);

}  // namespace tiny_renderer
