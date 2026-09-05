#include "tiny_renderer/obj_loader.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "tiny_renderer/mtl_loader.hpp"

namespace tiny_renderer {

ObjParseError::ObjParseError(std::size_t line, const std::string& message)
    : std::runtime_error("OBJ line " + std::to_string(line) + ": " + message), line_(line) {}

namespace {

enum class FaceLayout {
    Unknown,
    PositionTexcoord,
    PositionTexcoordNormal,
};

struct FaceReference {
    std::int64_t position{};
    std::int64_t texcoord{};
    std::int64_t normal{};
    FaceLayout layout{FaceLayout::Unknown};
};

struct UsedMaterial {
    std::string name;
    std::size_t line{};
};

struct MaterialMetadata {
    std::optional<std::string> library_filename;
    std::vector<UsedMaterial> used_materials;
    std::vector<std::string> face_materials;
};

constexpr std::size_t kMissingNormalIndex = std::numeric_limits<std::size_t>::max();

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
    const std::size_t first_slash = token.find('/');
    if (first_slash == std::string::npos || first_slash == 0U || first_slash + 1U >= token.size()) {
        fail(line, "triangle faces must use v/vt or v/vt/vn references");
    }

    const std::size_t second_slash = token.find('/', first_slash + 1U);
    if (second_slash == std::string::npos) {
        return {
            parse_positive_index(std::string_view(token).substr(0U, first_slash), line, "position"),
            parse_positive_index(std::string_view(token).substr(first_slash + 1U), line, "texture-coordinate"),
            0,
            FaceLayout::PositionTexcoord,
        };
    }

    if (token.find('/', second_slash + 1U) != std::string::npos
        || second_slash == first_slash + 1U
        || second_slash + 1U >= token.size()) {
        fail(line, "normal-bearing triangle faces must use complete v/vt/vn references");
    }

    return {
        parse_positive_index(std::string_view(token).substr(0U, first_slash), line, "position"),
        parse_positive_index(
            std::string_view(token).substr(first_slash + 1U, second_slash - first_slash - 1U),
            line,
            "texture-coordinate"),
        parse_positive_index(std::string_view(token).substr(second_slash + 1U), line, "normal"),
        FaceLayout::PositionTexcoordNormal,
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

void validate_nonzero_normal(const Vec3& normal, std::size_t line) {
    const double x = static_cast<double>(normal.x);
    const double y = static_cast<double>(normal.y);
    const double z = static_cast<double>(normal.z);
    const double length_squared = x * x + y * y + z * z;
    const double epsilon_squared = static_cast<double>(kEpsilon) * static_cast<double>(kEpsilon);
    if (length_squared <= epsilon_squared) {
        fail(line, "normal vector must be non-zero");
    }
}

void validate_sibling_library_filename(const std::string& token, std::size_t line) {
    const std::filesystem::path path(token);
    if (token.empty() || path.is_absolute() || path.has_parent_path()
        || token.find('/') != std::string::npos || token.find('\\') != std::string::npos
        || token == "." || token == "..") {
        fail(line, "mtllib must name exactly one sibling material file");
    }
}

MaterialMetadata scan_material_metadata(std::istream& input) {
    MaterialMetadata metadata;
    std::optional<std::string> active_material;
    bool saw_face = false;

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

        if (directive == "mtllib") {
            std::string filename;
            std::string extra;
            if (!(line >> filename) || (line >> extra)) {
                fail(line_number, "mtllib must contain exactly one filename");
            }
            if (saw_face) {
                fail(line_number, "mtllib must appear before material-bound faces");
            }
            if (metadata.library_filename) {
                fail(line_number, "only one mtllib directive is supported");
            }
            validate_sibling_library_filename(filename, line_number);
            metadata.library_filename = filename;
            continue;
        }

        if (directive == "usemtl") {
            std::string name;
            std::string extra;
            if (!(line >> name) || (line >> extra)) {
                fail(line_number, "usemtl must contain exactly one material name");
            }
            if (!metadata.library_filename) {
                fail(line_number, "usemtl requires a preceding mtllib directive");
            }
            active_material = name;
            metadata.used_materials.push_back({name, line_number});
            continue;
        }

        if (directive == "f") {
            saw_face = true;
            if (metadata.library_filename && !active_material) {
                fail(line_number, "material-aware OBJ faces require an active usemtl material");
            }
            metadata.face_materials.push_back(active_material.value_or(std::string{}));
        }
    }

    if (input.bad()) {
        throw std::runtime_error("failed while reading OBJ material metadata");
    }
    return metadata;
}

}  // namespace

Mesh load_obj(std::istream& input) {
    std::vector<Vec3> positions;
    std::vector<Vec2> texcoords;
    std::vector<Vec3> normals;
    std::map<std::array<std::size_t, 3>, std::uint32_t> unified_indices;
    FaceLayout mesh_layout = FaceLayout::Unknown;
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

        if (directive == "vn") {
            std::string x_token;
            std::string y_token;
            std::string z_token;
            std::string extra;
            if (!(line >> x_token >> y_token >> z_token) || (line >> extra)) {
                fail(line_number, "normal record must contain exactly three coordinates");
            }
            const Vec3 normal{
                parse_float(x_token, line_number, "normal x"),
                parse_float(y_token, line_number, "normal y"),
                parse_float(z_token, line_number, "normal z"),
            };
            validate_nonzero_normal(normal, line_number);
            normals.push_back(normal);
            continue;
        }

        if (directive == "f") {
            std::array<std::string, 3> corner_tokens;
            std::string extra;
            if (!(line >> corner_tokens[0] >> corner_tokens[1] >> corner_tokens[2]) || (line >> extra)) {
                fail(line_number, "only triangle faces with exactly three corners are supported");
            }

            std::array<FaceReference, 3> references{};
            for (std::size_t corner = 0U; corner < corner_tokens.size(); ++corner) {
                references[corner] = parse_face_reference(corner_tokens[corner], line_number);
            }
            const FaceLayout face_layout = references[0].layout;
            for (const FaceReference& reference : references) {
                if (reference.layout != face_layout) {
                    fail(line_number, "all corners of an OBJ face must use the same index layout");
                }
            }
            if (mesh_layout == FaceLayout::Unknown) {
                mesh_layout = face_layout;
            } else if (mesh_layout != face_layout) {
                fail(line_number, "mixing v/vt and v/vt/vn face layouts in one OBJ mesh is not supported");
            }

            TriangleIndices triangle{};
            for (std::size_t corner = 0U; corner < references.size(); ++corner) {
                const FaceReference& reference = references[corner];
                const std::size_t position_index =
                    resolve_index(reference.position, positions.size(), line_number, "position");
                const std::size_t texcoord_index =
                    resolve_index(reference.texcoord, texcoords.size(), line_number, "texture-coordinate");
                std::size_t normal_index = kMissingNormalIndex;
                if (face_layout == FaceLayout::PositionTexcoordNormal) {
                    normal_index = resolve_index(reference.normal, normals.size(), line_number, "normal");
                }
                const std::array<std::size_t, 3> key{position_index, texcoord_index, normal_index};

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
                if (face_layout == FaceLayout::PositionTexcoordNormal) {
                    const Vec3& normal = normals[normal_index];
                    mesh.vertices.push_back(Vertex::with_varyings(
                        positions[position_index],
                        VaryingPack{uv.x, uv.y, normal.x, normal.y, normal.z}));
                } else {
                    mesh.vertices.push_back(Vertex::with_varyings(
                        positions[position_index],
                        VaryingPack{uv.x, uv.y}));
                }
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

std::vector<MaterialBatch> load_obj_material_batches_file(const std::filesystem::path& path) {
    const Mesh geometry = load_obj_file(path);

    std::ifstream metadata_input(path);
    if (!metadata_input) {
        throw std::runtime_error("failed to reopen OBJ file for material metadata: " + path.string());
    }
    const MaterialMetadata metadata = scan_material_metadata(metadata_input);
    if (metadata.face_materials.size() != geometry.triangles.size()) {
        throw std::runtime_error("OBJ geometry/material face count changed between parsing passes");
    }

    if (!metadata.library_filename) {
        if (geometry.triangles.empty()) {
            return {};
        }
        return {MaterialBatch{geometry, std::string{}, MaterialState{}}};
    }

    const std::filesystem::path library_path = path.parent_path() / *metadata.library_filename;
    const MaterialLibrary library = load_mtl_file(library_path);
    for (const UsedMaterial& used : metadata.used_materials) {
        if (library.find(used.name) == library.end()) {
            fail(used.line, "usemtl references unknown material '" + used.name + "'");
        }
    }

    std::vector<MaterialBatch> batches;
    for (std::size_t face = 0U; face < geometry.triangles.size(); ++face) {
        const std::string& material_name = metadata.face_materials[face];
        const MaterialState material = library.at(material_name);
        if (batches.empty() || batches.back().material_name != material_name) {
            MaterialBatch batch;
            batch.mesh.vertices = geometry.vertices;
            batch.material_name = material_name;
            batch.material = material;
            batches.push_back(std::move(batch));
        }
        batches.back().mesh.triangles.push_back(geometry.triangles[face]);
    }
    return batches;
}

}  // namespace tiny_renderer
