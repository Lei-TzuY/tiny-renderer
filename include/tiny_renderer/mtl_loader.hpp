#pragma once

#include <cstddef>
#include <filesystem>
#include <istream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>

#include "tiny_renderer/material.hpp"

namespace tiny_renderer {

using MaterialLibrary = std::map<std::string, MaterialState>;

struct MaterialAssetDefinition {
    MaterialState material{};
    std::optional<std::string> diffuse_map_filename;
};

using MaterialAssetLibrary = std::map<std::string, MaterialAssetDefinition>;

class MtlParseError : public std::runtime_error {
public:
    MtlParseError(std::size_t line, const std::string& message);

    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_{};
};

[[nodiscard]] MaterialLibrary load_mtl(std::istream& input);
[[nodiscard]] MaterialLibrary load_mtl_file(const std::filesystem::path& path);

[[nodiscard]] MaterialAssetLibrary load_mtl_assets(std::istream& input);
[[nodiscard]] MaterialAssetLibrary load_mtl_assets_file(const std::filesystem::path& path);

}  // namespace tiny_renderer
