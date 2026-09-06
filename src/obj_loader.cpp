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
#include <tuple>
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

enum class MaterialMetadataMode {
    Ignore,
    CaptureStrict,
};

enum class MissingNormalMode {
    Preserve,
    Generate,
};

struct FaceReference {
    std::int64_t position{};
    std::int64_t texcoord{};
    std::int64_t normal{};
    FaceLayout layout{FaceLayout::Unknown};
};

struct ResolvedFaceReference {
    std::size_t position{};
    std::size_t texcoord{};
    std::size_t normal{};
};

struct GeneratedNormalDomain {
    bool flat{};
    std::uint64_t id{};
};

struct DoubleVec3 {
    double x{};
    double y{};
    double z{};
};

struct NormalAccumulation {
    DoubleVec3 value{};
    std::size_t first_line{};
};

using UnifiedVertexKey =
    std::tuple<std::size_t, std::size_t, std::size_t, bool, std::uint64_t>;
using GeneratedNormalKey = std::tuple<std::size_t, bool, std::uint64_t>;

constexpr std::size_t kMissingNormalIndex = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kMaxFaceCorners = 64U;

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

std::int64_t parse_index(std::string_view token, std::size_t line, const char* field) {
    std::int64_t value{};
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end) {
        fail(line, std::string("invalid ") + field + " index '" + std::string(token) + "'");
    }
    if (value == 0) {
        fail(line, std::string(field) + " OBJ index must not be zero");
    }
    return value;
}

FaceReference parse_face_reference(const std::string& token, std::size_t line) {
    const std::size_t first_slash = token.find('/');
    if (first_slash == std::string::npos || first_slash == 0U || first_slash + 1U >= token.size()) {
        fail(line, "polygon faces must use v/vt or v/vt/vn references");
    }

    const std::size_t second_slash = token.find('/', first_slash + 1U);
    if (second_slash == std::string::npos) {
        return {
            parse_index(std::string_view(token).substr(0U, first_slash), line, "position"),
            parse_index(std::string_view(token).substr(first_slash + 1U), line, "texture-coordinate"),
            0,
            FaceLayout::PositionTexcoord,
        };
    }

    if (token.find('/', second_slash + 1U) != std::string::npos
        || second_slash == first_slash + 1U
        || second_slash + 1U >= token.size()) {
        fail(line, "normal-bearing polygon faces must use complete v/vt/vn references");
    }

    return {
        parse_index(std::string_view(token).substr(0U, first_slash), line, "position"),
        parse_index(
            std::string_view(token).substr(first_slash + 1U, second_slash - first_slash - 1U),
            line,
            "texture-coordinate"),
        parse_index(std::string_view(token).substr(second_slash + 1U), line, "normal"),
        FaceLayout::PositionTexcoordNormal,
    };
}

std::size_t resolve_index(std::int64_t index, std::size_t extent, std::size_t line, const char* field) {
    if (index > 0) {
        const std::uint64_t positive = static_cast<std::uint64_t>(index);
        if (positive > static_cast<std::uint64_t>(extent)) {
            fail(line, std::string(field) + " index is out of range");
        }
        return static_cast<std::size_t>(positive - 1U);
    }

    if (index == std::numeric_limits<std::int64_t>::min()) {
        fail(line, std::string(field) + " relative index is out of range");
    }
    const std::uint64_t magnitude = static_cast<std::uint64_t>(-index);
    if (magnitude > static_cast<std::uint64_t>(extent)) {
        fail(line, std::string(field) + " relative index is out of range");
    }
    return extent - static_cast<std::size_t>(magnitude);
}

bool is_ignored_metadata_directive(const std::string& directive) {
    return directive == "o" || directive == "g";
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

std::optional<std::uint32_t> parse_smoothing_group(std::istringstream& line, std::size_t line_number) {
    std::string token;
    std::string extra;
    if (!(line >> token) || (line >> extra)) {
        fail(line_number, "s must contain exactly one smoothing-group token");
    }
    if (token == "off" || token == "0") {
        return std::nullopt;
    }
    if (token == "on") {
        return 1U;
    }

    std::uint64_t value{};
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || ptr != end || value == 0U
        || value > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        fail(line_number, "smoothing group must be off, on, 0, or a positive uint32 id");
    }
    return static_cast<std::uint32_t>(value);
}

void validate_sibling_library_filename(const std::string& token, std::size_t line) {
    const std::filesystem::path path(token);
    if (token.empty() || path.is_absolute() || path.has_parent_path()
        || token.find('/') != std::string::npos || token.find('\\') != std::string::npos
        || token == "." || token == "..") {
        fail(line, "mtllib must name exactly one sibling material file");
    }
}

DoubleVec3 polygon_area_vector(
    const std::vector<ResolvedFaceReference>& references,
    const std::vector<Vec3>& positions,
    std::size_t line) {
    const Vec3& origin = positions[references.front().position];
    DoubleVec3 sum{};
    for (std::size_t corner = 1U; corner + 1U < references.size(); ++corner) {
        const Vec3& b = positions[references[corner].position];
        const Vec3& c = positions[references[corner + 1U].position];
        const double ab_x = static_cast<double>(b.x) - static_cast<double>(origin.x);
        const double ab_y = static_cast<double>(b.y) - static_cast<double>(origin.y);
        const double ab_z = static_cast<double>(b.z) - static_cast<double>(origin.z);
        const double ac_x = static_cast<double>(c.x) - static_cast<double>(origin.x);
        const double ac_y = static_cast<double>(c.y) - static_cast<double>(origin.y);
        const double ac_z = static_cast<double>(c.z) - static_cast<double>(origin.z);
        sum.x += ab_y * ac_z - ab_z * ac_y;
        sum.y += ab_z * ac_x - ab_x * ac_z;
        sum.z += ab_x * ac_y - ab_y * ac_x;
    }

    const double length_squared = sum.x * sum.x + sum.y * sum.y + sum.z * sum.z;
    const double epsilon_squared = static_cast<double>(kEpsilon) * static_cast<double>(kEpsilon);
    if (!std::isfinite(sum.x) || !std::isfinite(sum.y) || !std::isfinite(sum.z)
        || !std::isfinite(length_squared)) {
        fail(line, "generated polygon normal exceeds finite range");
    }
    if (length_squared <= epsilon_squared) {
        fail(line, "cannot generate a normal for a degenerate polygon face");
    }
    return sum;
}

void accumulate_generated_normal(
    std::map<GeneratedNormalKey, NormalAccumulation>& accumulations,
    const GeneratedNormalKey& key,
    const DoubleVec3& face_normal,
    std::size_t line) {
    auto [it, inserted] = accumulations.try_emplace(key);
    if (inserted) {
        it->second.first_line = line;
    }
    it->second.value.x += face_normal.x;
    it->second.value.y += face_normal.y;
    it->second.value.z += face_normal.z;
    if (!std::isfinite(it->second.value.x)
        || !std::isfinite(it->second.value.y)
        || !std::isfinite(it->second.value.z)) {
        fail(line, "generated smoothing normal accumulation exceeds finite range");
    }
}

Vec3 normalized_generated_normal(const NormalAccumulation& accumulation) {
    const DoubleVec3& value = accumulation.value;
    const double length_squared = value.x * value.x + value.y * value.y + value.z * value.z;
    const double epsilon_squared = static_cast<double>(kEpsilon) * static_cast<double>(kEpsilon);
    if (!std::isfinite(length_squared) || length_squared <= epsilon_squared) {
        fail(accumulation.first_line, "generated smoothing normal is numerically degenerate");
    }
    const double inverse_length = 1.0 / std::sqrt(length_squared);
    const Vec3 normal{
        static_cast<float>(value.x * inverse_length),
        static_cast<float>(value.y * inverse_length),
        static_cast<float>(value.z * inverse_length),
    };
    if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z)) {
        fail(accumulation.first_line, "generated smoothing normal exceeds finite float range");
    }
    return normal;
}

void finalize_generated_normals(
    Mesh& mesh,
    const std::vector<std::optional<GeneratedNormalKey>>& vertex_normal_keys,
    const std::map<GeneratedNormalKey, NormalAccumulation>& accumulations) {
    if (vertex_normal_keys.size() != mesh.vertices.size()) {
        throw std::logic_error("generated OBJ normal bookkeeping lost vertex alignment");
    }
    for (std::size_t vertex_index = 0U; vertex_index < mesh.vertices.size(); ++vertex_index) {
        const auto& key = vertex_normal_keys[vertex_index];
        if (!key) {
            continue;
        }
        const auto accumulation = accumulations.find(*key);
        if (accumulation == accumulations.end()) {
            throw std::logic_error("generated OBJ vertex has no normal accumulation");
        }
        const Vec3 normal = normalized_generated_normal(accumulation->second);
        VaryingPack& varyings = mesh.vertices[vertex_index].varyings;
        if (varyings.count != 5U) {
            throw std::logic_error("generated OBJ normal vertex must own UV plus normal varyings");
        }
        varyings.values[2] = normal.x;
        varyings.values[3] = normal.y;
        varyings.values[4] = normal.z;
    }
}

ObjModelSource parse_obj(
    std::istream& input,
    MaterialMetadataMode material_mode,
    MissingNormalMode missing_normal_mode) {
    std::vector<Vec3> positions;
    std::vector<Vec2> texcoords;
    std::vector<Vec3> normals;
    std::map<UnifiedVertexKey, std::uint32_t> unified_indices;
    std::map<GeneratedNormalKey, NormalAccumulation> generated_normal_accumulations;
    std::vector<std::optional<GeneratedNormalKey>> vertex_generated_normal_keys;
    FaceLayout mesh_layout = FaceLayout::Unknown;
    ObjModelSource result;
    std::optional<std::string> active_material;
    std::optional<std::uint32_t> active_smoothing_group;
    std::uint64_t generated_flat_face_serial = 0U;
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
            if (material_mode == MaterialMetadataMode::Ignore) {
                continue;
            }
            std::string filename;
            std::string extra;
            if (!(line >> filename) || (line >> extra)) {
                fail(line_number, "mtllib must contain exactly one filename");
            }
            if (saw_face) {
                fail(line_number, "mtllib must appear before material-bound faces");
            }
            if (result.material_library_filename) {
                fail(line_number, "only one mtllib directive is supported");
            }
            validate_sibling_library_filename(filename, line_number);
            result.material_library_filename = filename;
            continue;
        }

        if (directive == "usemtl") {
            if (material_mode == MaterialMetadataMode::Ignore) {
                continue;
            }
            std::string name;
            std::string extra;
            if (!(line >> name) || (line >> extra)) {
                fail(line_number, "usemtl must contain exactly one material name");
            }
            if (!result.material_library_filename) {
                fail(line_number, "usemtl requires a preceding mtllib directive");
            }
            active_material = name;
            result.used_materials.push_back({name, line_number});
            continue;
        }

        if (directive == "s") {
            if (missing_normal_mode == MissingNormalMode::Preserve) {
                continue;
            }
            active_smoothing_group = parse_smoothing_group(line, line_number);
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
            std::vector<std::string> corner_tokens;
            corner_tokens.reserve(kMaxFaceCorners);
            std::string corner_token;
            while (line >> corner_token) {
                if (corner_tokens.size() == kMaxFaceCorners) {
                    fail(line_number, "OBJ polygon faces support at most 64 corners");
                }
                corner_tokens.push_back(std::move(corner_token));
            }
            if (corner_tokens.size() < 3U) {
                fail(line_number, "OBJ polygon faces require at least three corners");
            }

            std::vector<FaceReference> references;
            references.reserve(corner_tokens.size());
            for (const std::string& token : corner_tokens) {
                references.push_back(parse_face_reference(token, line_number));
            }
            const FaceLayout face_layout = references.front().layout;
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

            std::string material_name;
            if (material_mode == MaterialMetadataMode::CaptureStrict) {
                saw_face = true;
                if (result.material_library_filename && !active_material) {
                    fail(line_number, "material-aware OBJ faces require an active usemtl material");
                }
                material_name = active_material.value_or(std::string{});
            }

            std::vector<ResolvedFaceReference> resolved;
            resolved.reserve(references.size());
            for (const FaceReference& reference : references) {
                ResolvedFaceReference item;
                item.position = resolve_index(reference.position, positions.size(), line_number, "position");
                item.texcoord = resolve_index(
                    reference.texcoord,
                    texcoords.size(),
                    line_number,
                    "texture-coordinate");
                item.normal = kMissingNormalIndex;
                if (face_layout == FaceLayout::PositionTexcoordNormal) {
                    item.normal = resolve_index(reference.normal, normals.size(), line_number, "normal");
                }
                resolved.push_back(item);
            }

            std::optional<GeneratedNormalDomain> generated_domain;
            if (missing_normal_mode == MissingNormalMode::Generate
                && face_layout == FaceLayout::PositionTexcoord) {
                if (generated_flat_face_serial == std::numeric_limits<std::uint64_t>::max()) {
                    fail(line_number, "generated flat-normal face id exceeds uint64 capacity");
                }
                ++generated_flat_face_serial;
                generated_domain = active_smoothing_group
                    ? GeneratedNormalDomain{false, static_cast<std::uint64_t>(*active_smoothing_group)}
                    : GeneratedNormalDomain{true, generated_flat_face_serial};

                const DoubleVec3 face_normal = polygon_area_vector(resolved, positions, line_number);
                for (std::size_t corner = 0U; corner < resolved.size(); ++corner) {
                    bool already_accumulated = false;
                    for (std::size_t prior = 0U; prior < corner; ++prior) {
                        if (resolved[prior].position == resolved[corner].position) {
                            already_accumulated = true;
                            break;
                        }
                    }
                    if (already_accumulated) {
                        continue;
                    }
                    accumulate_generated_normal(
                        generated_normal_accumulations,
                        GeneratedNormalKey{
                            resolved[corner].position,
                            generated_domain->flat,
                            generated_domain->id,
                        },
                        face_normal,
                        line_number);
                }
            }

            std::vector<std::uint32_t> face_indices;
            face_indices.reserve(resolved.size());
            for (const ResolvedFaceReference& reference : resolved) {
                const bool generated = generated_domain.has_value();
                const bool generated_flat = generated && generated_domain->flat;
                const std::uint64_t generated_id = generated ? generated_domain->id : 0U;
                const UnifiedVertexKey key{
                    reference.position,
                    reference.texcoord,
                    reference.normal,
                    generated_flat,
                    generated_id,
                };

                std::uint32_t unified_index{};
                const auto existing = unified_indices.find(key);
                if (existing != unified_indices.end()) {
                    unified_index = existing->second;
                } else {
                    if (result.mesh.vertices.size()
                        > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                        fail(line_number, "unified vertex count exceeds uint32 index capacity");
                    }
                    unified_index = static_cast<std::uint32_t>(result.mesh.vertices.size());
                    const Vec2& uv = texcoords[reference.texcoord];
                    if (face_layout == FaceLayout::PositionTexcoordNormal) {
                        const Vec3& normal = normals[reference.normal];
                        result.mesh.vertices.push_back(Vertex::with_varyings(
                            positions[reference.position],
                            VaryingPack{uv.x, uv.y, normal.x, normal.y, normal.z}));
                        vertex_generated_normal_keys.push_back(std::nullopt);
                    } else if (generated) {
                        result.mesh.vertices.push_back(Vertex::with_varyings(
                            positions[reference.position],
                            VaryingPack{uv.x, uv.y, 0.0F, 0.0F, 0.0F}));
                        vertex_generated_normal_keys.push_back(GeneratedNormalKey{
                            reference.position,
                            generated_domain->flat,
                            generated_domain->id,
                        });
                    } else {
                        result.mesh.vertices.push_back(Vertex::with_varyings(
                            positions[reference.position],
                            VaryingPack{uv.x, uv.y}));
                        vertex_generated_normal_keys.push_back(std::nullopt);
                    }
                    unified_indices.emplace(key, unified_index);
                }

                for (const std::uint32_t prior : face_indices) {
                    if (prior == unified_index) {
                        fail(line_number, "OBJ polygon face must not repeat a corner");
                    }
                }
                face_indices.push_back(unified_index);
            }

            for (std::size_t corner = 1U; corner + 1U < face_indices.size(); ++corner) {
                result.mesh.triangles.push_back(
                    TriangleIndices{face_indices[0], face_indices[corner], face_indices[corner + 1U]});
                if (material_mode == MaterialMetadataMode::CaptureStrict) {
                    result.face_materials.push_back(material_name);
                }
            }
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
    if (missing_normal_mode == MissingNormalMode::Generate) {
        finalize_generated_normals(
            result.mesh,
            vertex_generated_normal_keys,
            generated_normal_accumulations);
    }
    return result;
}

}  // namespace

Mesh load_obj(std::istream& input) {
    return parse_obj(input, MaterialMetadataMode::Ignore, MissingNormalMode::Preserve).mesh;
}

Mesh load_obj_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open OBJ file: " + path.string());
    }
    return load_obj(input);
}

ObjModelSource load_obj_model_source(std::istream& input) {
    return parse_obj(input, MaterialMetadataMode::CaptureStrict, MissingNormalMode::Generate);
}

ObjModelSource load_obj_model_source_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open OBJ file: " + path.string());
    }
    return load_obj_model_source(input);
}

std::vector<MaterialBatch> load_obj_material_batches_file(const std::filesystem::path& path) {
    const ObjModelSource source = load_obj_model_source_file(path);
    const Mesh& geometry = source.mesh;

    if (!source.material_library_filename) {
        if (geometry.triangles.empty()) {
            return {};
        }
        return {MaterialBatch{geometry, std::string{}, MaterialState{}}};
    }

    const std::filesystem::path library_path = path.parent_path() / *source.material_library_filename;
    const MaterialLibrary library = load_mtl_file(library_path);
    for (const ObjMaterialUse& used : source.used_materials) {
        if (library.find(used.name) == library.end()) {
            fail(used.line, "usemtl references unknown material '" + used.name + "'");
        }
    }

    std::vector<MaterialBatch> batches;
    for (std::size_t face = 0U; face < geometry.triangles.size(); ++face) {
        const std::string& material_name = source.face_materials[face];
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
