#include "tiny_renderer/obj_loader.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace tiny_renderer {

ObjParseError::ObjParseError(std::size_t line, const std::string& message)
    : std::runtime_error("OBJ line " + std::to_string(line) + ": " + message), line_(line) {}

namespace {

struct FaceReference {
    std::int64_t position{};
    std::int64_t texcoord{};
};

[[noreturn]] void fail(std::size_t line, const std::string& message) {
    throw ObjParseError(line, message);
}

float parse_float(const std::string& token, std::size_t line, const char* field) {
    float value{};
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value, std::chars_format::general);
    if (error != std::errc{} || ptr != end || !std::isfinite(value)) {
        fail(line, std::string("invalid finite ") + field + " value '" + token + "'");
    }
    return value;
}

std::int64_t parse_positive_index(std::string_view token, std::size_t line, const char* field) {
    std::int64_t value{};
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end) {
        fail(line, std::string("invalid ") + field + " index '" + std::string(token) + "'");
    }
    if (value <= 0) {
        fail(line, "only positive absolute OBJ indices are supported");
    }
    return value;
}

FaceReference parse_face_reference(const std::string& token, std::size_t line) {
    const std::size_t slash = token.find('/');
    if (slash == std::string::npos || slash == 0U || slash + 1U >= token.size()) {
        fail(line, "triangle faces must use v/vt references");
    }
    if (token.find('/', slash + 1U) != std::string::npos) {
        fail(line, "v/vt/vn face references are not supported by this milestone");
    }

    return {
        parse_positive_index(std::string_view(token).substr(0U, slash), line, "position"),
        parse_positive_index(std::string_view(token).substr(slash + 1U), line, "texture-coordinate"),
    };
}

std::size_t resolve_index(std::int64_t one_based, std::size_t extent, std::size_t line, const char* field) {
    const std::uint64_t positive = static_cast<std::uint64_t>(one_based);
    if (positive > static_cast<std::uint64_t>(extent)) {
        fail(line, std::string(field) + " index is out of range");
    }
    return static_cast<std::size_t>(positive - 1U);
}

bool is_ignored_metadata_directive(const std::string& directive) {
    return directive == "o" || directive == "g" || directive == "s"
        || directive == "usemtl" || directive == "mtllib";
}

}  // namespace

Mesh load_obj(std::istream& input) {
    std::vector<Vec3> positions;
    std::vector<Vec2> texcoords;
    std::map<std::pair<std::size_t, std::size_t>, std::uint32_t> unified_indices;
    Mesh mesh;

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

        if (directive == "v") {
            std::string x_token;
            std::string y_token;
            std::string z_token;
            std::string extra;
            if (!(line >> x_token >> y_token >> z_token) || (line >> extra)) {
                fail(line_number, "vertex record must contain exactly three coordinates");
            }
            positions.push_back({
                parse_float(x_token, line_number, "vertex x"),
                parse_float(y_token, line_number, "vertex y"),
                parse_float(z_token, line_number, "vertex z"),
            });
            continue;
        }

        if (directive == "vt") {
            std::string u_token;
            std::string v_token;
            std::string extra;
            if (!(line >> u_token >> v_token) || (line >> extra)) {
                fail(line_number, "texture-coordinate record must contain exactly two coordinates");
            }
            texcoords.push_back({
                parse_float(u_token, line_number, "texture u"),
                parse_float(v_token, line_number, "texture v"),
            });
            continue;
        }

        if (directive == "f") {
            std::array<std::string, 3> corner_tokens;
            std::string extra;
            if (!(line >> corner_tokens[0] >> corner_tokens[1] >> corner_tokens[2]) || (line >> extra)) {
                fail(line_number, "only triangle faces with exactly three corners are supported");
            }

            TriangleIndices triangle{};
            for (std::size_t corner = 0U; corner < corner_tokens.size(); ++corner) {
                const FaceReference reference = parse_face_reference(corner_tokens[corner], line_number);
                const std::size_t position_index =
                    resolve_index(reference.position, positions.size(), line_number, "position");
                const std::size_t texcoord_index =
                    resolve_index(reference.texcoord, texcoords.size(), line_number, "texture-coordinate");
                const std::pair<std::size_t, std::size_t> key{position_index, texcoord_index};

                const auto existing = unified_indices.find(key);
                if (existing != unified_indices.end()) {
                    triangle[corner] = existing->second;
                    continue;
                }

                if (mesh.vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                    fail(line_number, "unified vertex count exceeds uint32 index capacity");
                }
                const std::uint32_t unified_index = static_cast<std::uint32_t>(mesh.vertices.size());
                const Vec2& uv = texcoords[texcoord_index];
                mesh.vertices.push_back(Vertex::with_varyings(
                    positions[position_index],
                    VaryingPack{uv.x, uv.y}));
                unified_indices.emplace(key, unified_index);
                triangle[corner] = unified_index;
            }
            mesh.triangles.push_back(triangle);
            continue;
        }

        if (is_ignored_metadata_directive(directive)) {
            continue;
        }

        fail(line_number, "unsupported OBJ directive '" + directive + "'");
    }

    if (input.bad()) {
        throw std::runtime_error("failed while reading OBJ stream");
    }
    return mesh;
}

Mesh load_obj_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open OBJ file: " + path.string());
    }
    return load_obj(input);
}

}  // namespace tiny_renderer
