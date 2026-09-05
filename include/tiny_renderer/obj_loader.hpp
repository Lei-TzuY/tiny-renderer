#pragma once

#include <cstddef>
#include <filesystem>
#include <istream>
#include <stdexcept>
#include <string>

#include "tiny_renderer/mesh.hpp"

namespace tiny_renderer {

class ObjParseError : public std::runtime_error {
public:
    ObjParseError(std::size_t line, const std::string& message);

    [[nodiscard]] std::size_t line() const noexcept { return line_; }

private:
    std::size_t line_{};
};

[[nodiscard]] Mesh load_obj(std::istream& input);
[[nodiscard]] Mesh load_obj_file(const std::filesystem::path& path);

}  // namespace tiny_renderer
