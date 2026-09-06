#pragma once

#include <filesystem>
#include <istream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/texture.hpp"

namespace tiny_renderer {

class TgaParseError : public std::runtime_error {
public:
    explicit TgaParseError(const std::string& message) : std::runtime_error(message) {}
};

[[nodiscard]] Texture2D load_tga(std::istream& input);
[[nodiscard]] Texture2D load_tga_file(const std::filesystem::path& path);

}  // namespace tiny_renderer
