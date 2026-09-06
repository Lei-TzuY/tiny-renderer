#include "tiny_renderer/rasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "rasterizer_validation.hpp"
#include "vertex_program_internal.hpp"

namespace tiny_renderer {

float signed_area_twice(const Vec2& a, const Vec2& b, const Vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

std::optional<Vec3> barycentric_coordinates(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& p) {
    const float area = signed_area_twice(a, b, c);
    if (std::fabs(area) <= kEpsilon) {
        return std::nullopt;
    }
    const float w0 = signed_area_twice(b, c, p) / area;
    const float w1 = signed_area_twice(c, a, p) / area;
    const float w2 = 1.0F - w0 - w1;
    return Vec3{w0, w1, w2};
}

bool barycentric_inside(const Vec3& barycentric, float epsilon) {
    return barycentric.x >= -epsilon && barycentric.y >= -epsilon && barycentric.z >= -epsilon;
}

namespace {

constexpr std::int64_t kSubpixelScale = 256;
constexpr std::int64_t kSubpixelHalf = kSubpixelScale / 2;
constexpr std::int64_t kMaxFixedCoordinate = 2'000'000'000LL;
constexpr std::size_t kMaxRasterCoordinate =
    static_cast<std::size_t>((kMaxFixedCoordinate - kSubpixelHalf) / kSubpixelScale);
constexpr float kMaxTextureCoordinateMagnitude = 1.0e20F;

struct ClipVertex {
    Vec4 position;
    VaryingPack varyings;
    Vec4 light_clip_position{};
    bool has_light_clip{false};
};

struct ScreenVertex {
    Vec2 position;
    float ndc_z{};
    float inv_w{};
    VaryingPack interpolation_terms;
    Vec4 light_clip_times_inv_w{};
    bool has_light_clip{false};
};

struct FixedPoint2 {
    std::int64_t x{};
    std::int64_t y{};
};

struct SampleLocation {
    std::int64_t fixed_x{};
    std::int64_t fixed_y{};
    float x{};
    float y{};
};

struct ShadedFragment {
    Vec3 rgb;
    float opacity{1.0F};
    bool discard{false};
};

SampleLocation sample_location(SampleCount count, std::size_t sample_index) {
    if (count == SampleCount::One) {
        if (sample_index != 0U) {
            throw std::logic_error("single-sample raster requested an invalid sample index");
        }
        return {128, 128, 0.5F, 0.5F};
    }
    if (count == SampleCount::Four) {
        switch (sample_index) {
            case 0U: return {64, 64, 0.25F, 0.25F};
            case 1U: return {192, 64, 0.75F, 0.25F};
            case 2U: return {64, 192, 0.25F, 0.75F};
            case 3U: return {192, 192, 0.75F, 0.75F};
            default:
                throw std::logic_error("4x raster requested an invalid sample index");
        }
    }
    throw std::logic_error("raster target has an unsupported sample count");
}

std::size_t alpha_to_coverage_sample_count(float opacity) {
    if (!std::isfinite(opacity) || opacity < 0.0F || opacity > 1.0F) {
        throw std::logic_error("shaded fragment opacity must be finite and within [0, 1]");
    }
    const float rounded = std::floor(opacity * 4.0F + 0.5F);
    return std::min<std::size_t>(4U, static_cast<std::size_t>(rounded));
}

bool alpha_to_coverage_accepts(
    const AlphaToCoverageState& state,
    float opacity,
    std::size_t sample_index) {
    if (!state.enabled) {
        return true;
    }
    return sample_index < alpha_to_coverage_sample_count(opacity);
}

bool alpha_test_accepts(const AlphaTestState& state, float opacity) {
    if (!state.enabled) {
        return true;
    }
    return opacity >= state.threshold;
}

bool finite_vec3(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void validate_fragment_program(
    const FragmentProgramPtr& fragment_program,
    std::size_t varying_count) {
    if (fragment_program) {
        fragment_program->validate(varying_count);
    }
}

ShadedFragment run_fragment_program(
    const FragmentProgram* fragment_program,
    const VaryingPack& varyings,
    const ShadedFragment& fixed_fragment,
    const Vec2& sample_position,
    std::size_t sample_index,
    float depth) {
    if (fragment_program == nullptr) {
        return fixed_fragment;
    }

    const FragmentProgramOutput output = fragment_program->shade(FragmentProgramInput{
        varyings,
        fixed_fragment.rgb,
        fixed_fragment.opacity,
        sample_position,
        sample_index,
        depth,
    });
    if (!finite_vec3(output.rgb)) {
        throw std::invalid_argument("fragment program RGB output must be finite");
    }
    if (!std::isfinite(output.opacity) || output.opacity < 0.0F || output.opacity > 1.0F) {
        throw std::invalid_argument("fragment program opacity output must be finite and within [0, 1]");
    }
    return {output.rgb, output.opacity, output.discard};
}

void validate_pack(const VaryingPack& pack) {
    if (pack.count > kMaxVaryings) {
        throw std::invalid_argument("varying pack count exceeds fixed capacity");
    }
}

void validate_color_binding(const ColorBinding& binding, std::size_t varying_count) {
    if (binding.red >= varying_count || binding.green >= varying_count || binding.blue >= varying_count) {
        throw std::out_of_range("color binding references unavailable varying channel");
    }
}

void validate_texture_binding(const TextureBinding& binding, std::size_t varying_count) {
    if (binding.texture == nullptr && binding.opacity_texture == nullptr) {
        return;
    }
    if (binding.u_channel >= varying_count || binding.v_channel >= varying_count) {
        throw std::out_of_range("texture binding references unavailable varying channel");
    }
}

BaseColorSource prepare_base_color_source(BaseColorSource source, const TextureBinding& texture_binding) {
    switch (source) {
        case BaseColorSource::Auto:
            return texture_binding.texture != nullptr
                ? BaseColorSource::Texture
                : BaseColorSource::VaryingColor;
        case BaseColorSource::VaryingColor:
            if (texture_binding.texture != nullptr) {
                throw std::invalid_argument("varying-color base source conflicts with a bound texture");
            }
            return source;
        case BaseColorSource::Texture:
            if (texture_binding.texture == nullptr) {
                throw std::invalid_argument("texture base-color source requires a bound texture");
            }
            return source;
        case BaseColorSource::ConstantWhite:
            if (texture_binding.texture != nullptr) {
                throw std::invalid_argument("constant-white base-color source conflicts with a bound texture");
            }
            return source;
    }
    throw std::invalid_argument("unknown base-color source");
}

void validate_normal_binding(const NormalBinding& binding, const VaryingPack& pack) {
    if (binding.x >= pack.count || binding.y >= pack.count || binding.z >= pack.count) {
        throw std::out_of_range("normal binding references unavailable varying channel");
    }
    const Interpolation mode = pack.interpolation[binding.x];
    if (pack.interpolation[binding.y] != mode || pack.interpolation[binding.z] != mode) {
        throw std::invalid_argument("normal binding channels must use one interpolation qualifier");
    }
}

void validate_output_binding(
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    BaseColorSource source,
    std::size_t varying_count) {
    switch (source) {
        case BaseColorSource::VaryingColor:
            validate_color_binding(color_binding, varying_count);
            break;
        case BaseColorSource::Texture:
            break;
        case BaseColorSource::ConstantWhite:
            break;
        case BaseColorSource::Auto:
            throw std::logic_error("automatic base-color source must be resolved before validation");
    }
    if (source == BaseColorSource::Texture || texture_binding.opacity_texture != nullptr) {
        validate_texture_binding(texture_binding, varying_count);
    }
}

void validate_texture_coordinates(
    const VaryingPack& pack,
    const TextureBinding& binding,
    BaseColorSource source) {
    if (source != BaseColorSource::Texture && binding.opacity_texture == nullptr) {
        return;
    }
    const float u = pack.values[binding.u_channel];
    const float v = pack.values[binding.v_channel];
    if (!std::isfinite(u) || !std::isfinite(v)
        || std::fabs(u) > kMaxTextureCoordinateMagnitude
        || std::fabs(v) > kMaxTextureCoordinateMagnitude) {
        throw std::invalid_argument("texture coordinates exceed safe finite range");
    }
}

void validate_normal_value(const VaryingPack& pack, const NormalBinding& binding) {
    const Vec3 normal{
        pack.values[binding.x],
        pack.values[binding.y],
        pack.values[binding.z],
    };
    if (!finite_vec3(normal) || length(normal) <= kEpsilon) {
        throw std::invalid_argument("vertex normal must be finite and non-zero");
    }
}

DirectionalLight prepare_directional_light(const DirectionalLight& light) {
    if (!light.enabled) {
        return light;
    }
    if (!finite_vec3(light.direction_to_light) || length(light.direction_to_light) <= kEpsilon) {
        throw std::invalid_argument("directional light direction must be finite and non-zero");
    }
    if (!std::isfinite(light.ambient) || !std::isfinite(light.diffuse)
        || light.ambient < 0.0F || light.diffuse < 0.0F
        || light.ambient > 1.0F || light.diffuse > 1.0F
        || light.ambient + light.diffuse > 1.0F + kEpsilon) {
        throw std::invalid_argument("directional light coefficients must be finite, non-negative, and sum to at most one");
    }
    DirectionalLight prepared = light;
    prepared.direction_to_light = normalize(light.direction_to_light);
    return prepared;
}

MaterialState prepare_material_state(const MaterialState& material) {
    if (!finite_vec3(material.albedo)
        || material.albedo.x < 0.0F || material.albedo.x > 1.0F
        || material.albedo.y < 0.0F || material.albedo.y > 1.0F
        || material.albedo.z < 0.0F || material.albedo.z > 1.0F) {
        throw std::invalid_argument("material albedo components must be finite and within [0, 1]");
    }
    if (!std::isfinite(material.opacity) || material.opacity < 0.0F || material.opacity > 1.0F) {
        throw std::invalid_argument("material opacity must be finite and within [0, 1]");
    }
    return material;
}

void validate_layout_match(const VaryingPack& reference, const VaryingPack& candidate) {
    validate_pack(candidate);
    if (candidate.count != reference.count) {
        throw std::invalid_argument("varying packs must use the same channel count");
    }
    for (std::size_t channel = 0; channel < reference.count; ++channel) {
        if (candidate.interpolation[channel] != reference.interpolation[channel]) {
            throw std::invalid_argument("varying packs must use the same interpolation qualifiers");
        }
    }
}

void validate_triangle_varyings(
    const Triangle& triangle,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    BaseColorSource source,
    const DirectionalLight& light) {
    validate_pack(triangle[0].varyings);
    validate_output_binding(color_binding, texture_binding, source, triangle[0].varyings.count);
    validate_layout_match(triangle[0].varyings, triangle[1].varyings);
    validate_layout_match(triangle[0].varyings, triangle[2].varyings);
    if (light.enabled) {
        validate_normal_binding(light.normal, triangle[0].varyings);
    }
    for (const Vertex& vertex : triangle) {
        validate_texture_coordinates(vertex.varyings, texture_binding, source);
        if (light.enabled) {
            validate_normal_value(vertex.varyings, light.normal);
        }
    }
}

void validate_raster_target(const Framebuffer& framebuffer) {
    if (framebuffer.width() - 1U > kMaxRasterCoordinate || framebuffer.height() - 1U > kMaxRasterCoordinate) {
        throw std::overflow_error("framebuffer dimensions exceed fixed-point rasterizer safety bound");
    }
}

void transform_normal(VaryingPack& pack, const NormalBinding& binding, const Mat3& matrix) {
    const Vec3 transformed = matrix * Vec3{
        pack.values[binding.x],
        pack.values[binding.y],
        pack.values[binding.z],
    };
    if (!finite_vec3(transformed) || length(transformed) <= kEpsilon) {
        throw std::invalid_argument("transformed vertex normal is numerically unstable");
    }
    pack.values[binding.x] = transformed.x;
    pack.values[binding.y] = transformed.y;
    pack.values[binding.z] = transformed.z;
}

Triangle transform_triangle_normals(const Triangle& triangle, const NormalBinding& binding, const Mat3& matrix) {
    Triangle transformed = triangle;
    for (Vertex& vertex : transformed) {
        transform_normal(vertex.varyings, binding, matrix);
    }
    return transformed;
}

Mesh transform_mesh_normals(const Mesh& mesh, const NormalBinding& binding, const Mat3& matrix) {
    Mesh transformed = mesh;
    for (Vertex& vertex : transformed.vertices) {
        transform_normal(vertex.varyings, binding, matrix);
    }
    return transformed;
}

float lerp_scalar(float a, float b, float t) {
    return a + (b - a) * t;
}

VaryingPack interpolate_clip_varyings(
    const VaryingPack& a,
    const VaryingPack& b,
    float smooth_t,
    float noperspective_t) {
    validate_layout_match(a, b);
    VaryingPack result;
    result.count = a.count;
    result.interpolation = a.interpolation;
    for (std::size_t i = 0; i < result.count; ++i) {
        switch (a.interpolation[i]) {
            case Interpolation::Smooth:
                result.values[i] = lerp_scalar(a.values[i], b.values[i], smooth_t);
                break;
            case Interpolation::NoPerspective:
                result.values[i] = lerp_scalar(a.values[i], b.values[i], noperspective_t);
                break;
            case Interpolation::Flat:
                result.values[i] = a.values[i];
                break;
        }
    }
    return result;
}

void apply_flat_provoking_vertex(std::array<ClipVertex, 3>& triangle) {
    for (std::size_t channel = 0; channel < triangle[0].varyings.count; ++channel) {
        if (triangle[0].varyings.interpolation[channel] != Interpolation::Flat) {
            continue;
        }
        const float provoking_value = triangle[0].varyings.values[channel];
        for (ClipVertex& vertex : triangle) {
            vertex.varyings.values[channel] = provoking_value;
        }
    }
}

VaryingPack prepare_interpolation_terms(const VaryingPack& pack, float inv_w) {
    VaryingPack result = pack;
    for (std::size_t i = 0; i < result.count; ++i) {
        if (result.interpolation[i] == Interpolation::Smooth) {
            result.values[i] *= inv_w;
        }
    }
    return result;
}

float plane_distance(const ClipVertex& v, int plane) {
    switch (plane) {
        case 0: return v.position.w + v.position.x;
        case 1: return v.position.w - v.position.x;
        case 2: return v.position.w + v.position.y;
        case 3: return v.position.w - v.position.y;
        case 4: return v.position.w + v.position.z;
        case 5: return v.position.w - v.position.z;
        default: return -1.0F;
    }
}

ClipVertex lerp(const ClipVertex& a, const ClipVertex& b, float t) {
    const Vec4 position = a.position + (b.position - a.position) * t;
    float noperspective_t = t;
    if (std::fabs(position.w) > kEpsilon) {
        const float projected_t = (t * b.position.w) / position.w;
        if (std::isfinite(projected_t)) {
            noperspective_t = projected_t;
        }
    }
    if (a.has_light_clip != b.has_light_clip) {
        throw std::logic_error("clipped shadow coordinate availability mismatch");
    }
    const Vec4 light_clip_position = a.has_light_clip
        ? a.light_clip_position + (b.light_clip_position - a.light_clip_position) * t
        : Vec4{};
    return {
        position,
        interpolate_clip_varyings(a.varyings, b.varyings, t, noperspective_t),
        light_clip_position,
        a.has_light_clip,
    };
}

std::vector<ClipVertex> clip_against_plane(const std::vector<ClipVertex>& input, int plane) {
    std::vector<ClipVertex> output;
    if (input.empty()) {
        return output;
    }
    output.reserve(input.size() + 1U);

    ClipVertex previous = input.back();
    float previous_distance = plane_distance(previous, plane);
    bool previous_inside = previous_distance >= 0.0F;

    for (const ClipVertex& current : input) {
        const float current_distance = plane_distance(current, plane);
        const bool current_inside = current_distance >= 0.0F;

        if (current_inside != previous_inside) {
            const float denominator = previous_distance - current_distance;
            if (std::fabs(denominator) > kEpsilon) {
                const float t = previous_distance / denominator;
                output.push_back(lerp(previous, current, std::clamp(t, 0.0F, 1.0F)));
            }
        }
        if (current_inside) {
            output.push_back(current);
        }

        previous = current;
        previous_distance = current_distance;
        previous_inside = current_inside;
    }
    return output;
}

std::vector<ClipVertex> clip_triangle(const std::array<ClipVertex, 3>& triangle) {
    std::vector<ClipVertex> polygon(triangle.begin(), triangle.end());
    for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
        polygon = clip_against_plane(polygon, plane);
    }
    return polygon;
}

bool finite(const Vec4& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}

std::optional<Vec2> to_ndc_xy(const ClipVertex& vertex) {
    if (!finite(vertex.position) || std::fabs(vertex.position.w) <= kEpsilon) {
        return std::nullopt;
    }
    const float inv_w = 1.0F / vertex.position.w;
    const Vec2 result{vertex.position.x * inv_w, vertex.position.y * inv_w};
    if (!std::isfinite(result.x) || !std::isfinite(result.y)) {
        return std::nullopt;
    }
    return result;
}

bool should_cull_projected_triangle(
    const ClipVertex& a,
    const ClipVertex& b,
    const ClipVertex& c,
    CullMode cull_mode,
    FrontFace front_face) {
    if (cull_mode == CullMode::None) {
        return false;
    }

    const auto ndc_a = to_ndc_xy(a);
    const auto ndc_b = to_ndc_xy(b);
    const auto ndc_c = to_ndc_xy(c);
    if (!ndc_a || !ndc_b || !ndc_c) {
        return true;
    }

    const float area = signed_area_twice(*ndc_a, *ndc_b, *ndc_c);
    if (!std::isfinite(area) || std::fabs(area) <= kEpsilon) {
        return true;
    }

    const bool front_facing = front_face == FrontFace::CounterClockwise
        ? area > 0.0F
        : area < 0.0F;
    return cull_mode == CullMode::Back ? !front_facing : front_facing;
}

std::optional<ScreenVertex> to_screen(const ClipVertex& vertex, const RasterRect& viewport) {
    if (!finite(vertex.position) || std::fabs(vertex.position.w) <= kEpsilon) {
        return std::nullopt;
    }

    const float inv_w = 1.0F / vertex.position.w;
    const float ndc_x = vertex.position.x * inv_w;
    const float ndc_y = vertex.position.y * inv_w;
    const float ndc_z = vertex.position.z * inv_w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !std::isfinite(ndc_z)) {
        return std::nullopt;
    }

    const float origin_x = static_cast<float>(viewport.x);
    const float origin_y = static_cast<float>(viewport.y);
    const float max_x = static_cast<float>(viewport.width - 1U);
    const float max_y = static_cast<float>(viewport.height - 1U);
    return ScreenVertex{
        {
            origin_x + (ndc_x * 0.5F + 0.5F) * max_x,
            origin_y + (1.0F - (ndc_y * 0.5F + 0.5F)) * max_y,
        },
        ndc_z,
        inv_w,
        prepare_interpolation_terms(vertex.varyings, inv_w),
        vertex.has_light_clip ? vertex.light_clip_position * inv_w : Vec4{},
        vertex.has_light_clip,
    };
}

float edge(const Vec2& a, const Vec2& b, const Vec2& p) {
    return signed_area_twice(a, b, p);
}

std::int64_t quantize_subpixel(float value) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("non-finite screen coordinate");
    }
    const double scaled = static_cast<double>(value) * static_cast<double>(kSubpixelScale);
    if (scaled < -static_cast<double>(kMaxFixedCoordinate)
        || scaled > static_cast<double>(kMaxFixedCoordinate)) {
        throw std::overflow_error("screen coordinate exceeds fixed-point rasterizer safety bound");
    }
    return static_cast<std::int64_t>(std::llround(scaled));
}

FixedPoint2 quantize_subpixel(const Vec2& value) {
    return {quantize_subpixel(value.x), quantize_subpixel(value.y)};
}

std::int64_t fixed_edge(const FixedPoint2& a, const FixedPoint2& b, const FixedPoint2& p) {
    const std::int64_t dx_ab = b.x - a.x;
    const std::int64_t dy_ab = b.y - a.y;
    const std::int64_t dx_ap = p.x - a.x;
    const std::int64_t dy_ap = p.y - a.y;
    return dx_ab * dy_ap - dy_ab * dx_ap;
}

bool is_top_left(const FixedPoint2& a, const FixedPoint2& b) {
    const std::int64_t dy = b.y - a.y;
    const std::int64_t dx = b.x - a.x;
    return (dy < 0) || (dy == 0 && dx > 0);
}

bool edge_accept(std::int64_t value, bool top_left) {
    return value > 0 || (value == 0 && top_left);
}

VaryingPack interpolate_varyings(
    const std::array<ScreenVertex, 3>& v,
    const Vec3& bary,
    float reciprocal_w) {
    VaryingPack result;
    result.count = v[0].interpolation_terms.count;
    result.interpolation = v[0].interpolation_terms.interpolation;
    for (std::size_t i = 0; i < result.count; ++i) {
        switch (result.interpolation[i]) {
            case Interpolation::Smooth: {
                const float numerator = v[0].interpolation_terms.values[i] * bary.x
                    + v[1].interpolation_terms.values[i] * bary.y
                    + v[2].interpolation_terms.values[i] * bary.z;
                result.values[i] = numerator / reciprocal_w;
                break;
            }
            case Interpolation::NoPerspective:
                result.values[i] = v[0].interpolation_terms.values[i] * bary.x
                    + v[1].interpolation_terms.values[i] * bary.y
                    + v[2].interpolation_terms.values[i] * bary.z;
                break;
            case Interpolation::Flat:
                result.values[i] = v[0].interpolation_terms.values[i];
                break;
        }
    }
    return result;
}

Vec4 interpolate_light_clip(
    const std::array<ScreenVertex, 3>& v,
    const Vec3& bary,
    float reciprocal_w) {
    if (!v[0].has_light_clip || !v[1].has_light_clip || !v[2].has_light_clip) {
        throw std::logic_error("shadow shading requires a generated light clip coordinate");
    }
    const Vec4 numerator = v[0].light_clip_times_inv_w * bary.x
        + v[1].light_clip_times_inv_w * bary.y
        + v[2].light_clip_times_inv_w * bary.z;
    return numerator * (1.0F / reciprocal_w);
}

Vec3 base_fragment_color(
    const VaryingPack& varyings,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    BaseColorSource source) {
    switch (source) {
        case BaseColorSource::VaryingColor:
            return {
                varyings.values[color_binding.red],
                varyings.values[color_binding.green],
                varyings.values[color_binding.blue],
            };
        case BaseColorSource::Texture:
            return texture_binding.texture->sample(
                {varyings.values[texture_binding.u_channel], varyings.values[texture_binding.v_channel]},
                texture_binding.sampler);
        case BaseColorSource::ConstantWhite:
            return {1.0F, 1.0F, 1.0F};
        case BaseColorSource::Auto:
            throw std::logic_error("automatic base-color source must be resolved before shading");
    }
    throw std::logic_error("unreachable base-color source shading state");
}

float fragment_opacity(
    const VaryingPack& varyings,
    const TextureBinding& texture_binding,
    const MaterialState& material) {
    if (texture_binding.opacity_texture == nullptr) {
        return material.opacity;
    }
    const Vec3 sampled = texture_binding.opacity_texture->sample(
        {varyings.values[texture_binding.u_channel], varyings.values[texture_binding.v_channel]},
        texture_binding.sampler);
    // Texture2D stores RGB only. For bounded teaching-space opacity maps we use
    // the arithmetic mean of sampled linear RGB, clamped to [0,1]. This is a
    // deterministic scalar rule, not a luminance or colorimetric claim.
    const float map_opacity = std::clamp(
        (sampled.x + sampled.y + sampled.z) / 3.0F,
        0.0F,
        1.0F);
    return material.opacity * map_opacity;
}

float shadow_visibility(const ShadowState& shadow, const Vec4& light_clip) {
    if (!shadow.enabled) {
        return 1.0F;
    }
    if (!finite(light_clip) || light_clip.w <= kEpsilon) {
        return 1.0F;
    }
    if (light_clip.x < -light_clip.w || light_clip.x > light_clip.w
        || light_clip.y < -light_clip.w || light_clip.y > light_clip.w
        || light_clip.z < -light_clip.w || light_clip.z > light_clip.w) {
        return 1.0F;
    }
    if (!shadow.map) {
        throw std::logic_error("validated shadow state lost its depth texture");
    }

    const float inv_w = 1.0F / light_clip.w;
    const float ndc_x = light_clip.x * inv_w;
    const float ndc_y = light_clip.y * inv_w;
    const float ndc_z = light_clip.z * inv_w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !std::isfinite(ndc_z)
        || ndc_x < -1.0F || ndc_x > 1.0F
        || ndc_y < -1.0F || ndc_y > 1.0F
        || ndc_z < -1.0F || ndc_z > 1.0F) {
        return 1.0F;
    }

    const float map_max_x = static_cast<float>(shadow.map->width() - 1U);
    const float map_max_y = static_cast<float>(shadow.map->height() - 1U);
    const float map_x = (ndc_x * 0.5F + 0.5F) * map_max_x;
    const float map_y = (1.0F - (ndc_y * 0.5F + 0.5F)) * map_max_y;
    const std::size_t x = static_cast<std::size_t>(std::llround(map_x));
    const std::size_t y = static_cast<std::size_t>(std::llround(map_y));
    const float fragment_depth = ndc_z * 0.5F + 0.5F;
    const float stored_depth = shadow.map->depth_at(x, y);
    return fragment_depth - shadow.bias <= stored_depth ? 1.0F : 0.0F;
}

ShadedFragment shade_fragment(
    const VaryingPack& varyings,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    BaseColorSource source,
    const DirectionalLight& light,
    const MaterialState& material,
    const ShadowState& shadow,
    const Vec4& light_clip) {
    const Vec3 source_color = base_fragment_color(varyings, color_binding, texture_binding, source);
    const Vec3 base{
        source_color.x * material.albedo.x,
        source_color.y * material.albedo.y,
        source_color.z * material.albedo.z,
    };
    const float opacity = fragment_opacity(varyings, texture_binding, material);
    if (!light.enabled) {
        return {base, opacity, false};
    }

    const Vec3 interpolated_normal{
        varyings.values[light.normal.x],
        varyings.values[light.normal.y],
        varyings.values[light.normal.z],
    };
    if (!finite_vec3(interpolated_normal)) {
        return {base * light.ambient, opacity, false};
    }
    const float normal_length = length(interpolated_normal);
    if (!std::isfinite(normal_length) || normal_length <= kEpsilon) {
        return {base * light.ambient, opacity, false};
    }

    const Vec3 normal = interpolated_normal / normal_length;
    const float lambert = std::clamp(dot(normal, light.direction_to_light), 0.0F, 1.0F);
    const float visibility = shadow_visibility(shadow, light_clip);
    const float intensity = light.ambient + light.diffuse * lambert * visibility;
    return {base * intensity, opacity, false};
}

void rasterize_screen_triangle(
    Framebuffer& framebuffer,
    std::array<ScreenVertex, 3> v,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    BaseColorSource source,
    const DirectionalLight& light,
    const MaterialState& material,
    const DepthState& depth_state,
    const StencilState& stencil_state,
    const BlendState& blend_state,
    const AlphaToCoverageState& alpha_to_coverage_state,
    const AlphaTestState& alpha_test_state,
    const ShadowState& shadow_state,
    const FragmentProgram* fragment_program,
    const std::optional<RasterRect>& scissor) {
    std::array<FixedPoint2, 3> fixed{
        quantize_subpixel(v[0].position),
        quantize_subpixel(v[1].position),
        quantize_subpixel(v[2].position),
    };

    const std::int64_t fixed_area = fixed_edge(fixed[0], fixed[1], fixed[2]);
    if (fixed_area == 0) {
        return;
    }
    if (fixed_area < 0) {
        std::swap(v[1], v[2]);
        std::swap(fixed[1], fixed[2]);
    }

    const float interpolation_area = edge(v[0].position, v[1].position, v[2].position);
    if (!std::isfinite(interpolation_area) || std::fabs(interpolation_area) <= kEpsilon) {
        return;
    }

    const float min_x_f = std::min({v[0].position.x, v[1].position.x, v[2].position.x});
    const float max_x_f = std::max({v[0].position.x, v[1].position.x, v[2].position.x});
    const float min_y_f = std::min({v[0].position.y, v[1].position.y, v[2].position.y});
    const float max_y_f = std::max({v[0].position.y, v[1].position.y, v[2].position.y});

    const int width = static_cast<int>(framebuffer.width());
    const int height = static_cast<int>(framebuffer.height());
    int min_x = std::max(0, static_cast<int>(std::floor(min_x_f)));
    int max_x = std::min(width - 1, static_cast<int>(std::ceil(max_x_f)));
    int min_y = std::max(0, static_cast<int>(std::floor(min_y_f)));
    int max_y = std::min(height - 1, static_cast<int>(std::ceil(max_y_f)));

    if (scissor) {
        if (scissor->width == 0U || scissor->height == 0U) {
            return;
        }
        const int scissor_min_x = static_cast<int>(scissor->x);
        const int scissor_min_y = static_cast<int>(scissor->y);
        const int scissor_max_x = static_cast<int>(scissor->x + scissor->width - 1U);
        const int scissor_max_y = static_cast<int>(scissor->y + scissor->height - 1U);
        min_x = std::max(min_x, scissor_min_x);
        max_x = std::min(max_x, scissor_max_x);
        min_y = std::max(min_y, scissor_min_y);
        max_y = std::min(max_y, scissor_max_y);
    }

    if (min_x > max_x || min_y > max_y) {
        return;
    }

    const bool tl0 = is_top_left(fixed[1], fixed[2]);
    const bool tl1 = is_top_left(fixed[2], fixed[0]);
    const bool tl2 = is_top_left(fixed[0], fixed[1]);
    const std::size_t samples_per_pixel = framebuffer.samples_per_pixel();

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            for (std::size_t sample_index = 0U; sample_index < samples_per_pixel; ++sample_index) {
                const SampleLocation location = sample_location(framebuffer.sample_count(), sample_index);
                const FixedPoint2 sample{
                    static_cast<std::int64_t>(x) * kSubpixelScale + location.fixed_x,
                    static_cast<std::int64_t>(y) * kSubpixelScale + location.fixed_y,
                };
                const std::int64_t e0_fixed = fixed_edge(fixed[1], fixed[2], sample);
                const std::int64_t e1_fixed = fixed_edge(fixed[2], fixed[0], sample);
                const std::int64_t e2_fixed = fixed_edge(fixed[0], fixed[1], sample);
                if (!edge_accept(e0_fixed, tl0)
                    || !edge_accept(e1_fixed, tl1)
                    || !edge_accept(e2_fixed, tl2)) {
                    continue;
                }

                const Vec2 p{
                    static_cast<float>(x) + location.x,
                    static_cast<float>(y) + location.y,
                };
                const float e0 = edge(v[1].position, v[2].position, p);
                const float e1 = edge(v[2].position, v[0].position, p);
                const float e2 = edge(v[0].position, v[1].position, p);
                const Vec3 bary{e0 / interpolation_area, e1 / interpolation_area, e2 / interpolation_area};
                const float ndc_z = bary.x * v[0].ndc_z + bary.y * v[1].ndc_z + bary.z * v[2].ndc_z;
                const float depth = ndc_z * 0.5F + 0.5F;
                if (depth < 0.0F || depth > 1.0F || !std::isfinite(depth)) {
                    continue;
                }

                const float reciprocal_w = bary.x * v[0].inv_w + bary.y * v[1].inv_w + bary.z * v[2].inv_w;
                if (std::fabs(reciprocal_w) <= kEpsilon || !std::isfinite(reciprocal_w)) {
                    continue;
                }
                const VaryingPack varyings = interpolate_varyings(v, bary, reciprocal_w);
                const Vec4 light_clip = shadow_state.enabled
                    ? interpolate_light_clip(v, bary, reciprocal_w)
                    : Vec4{};
                const ShadedFragment fixed_fragment = shade_fragment(
                    varyings,
                    color_binding,
                    texture_binding,
                    source,
                    light,
                    material,
                    shadow_state,
                    light_clip);
                const ShadedFragment fragment = run_fragment_program(
                    fragment_program,
                    varyings,
                    fixed_fragment,
                    p,
                    sample_index,
                    depth);
                if (fragment.discard) {
                    continue;
                }
                if (!alpha_test_accepts(alpha_test_state, fragment.opacity)) {
                    continue;
                }
                if (!alpha_to_coverage_accepts(
                        alpha_to_coverage_state,
                        fragment.opacity,
                        sample_index)) {
                    continue;
                }
                framebuffer.test_and_write_sample(
                    static_cast<std::size_t>(x),
                    static_cast<std::size_t>(y),
                    sample_index,
                    depth,
                    fragment.rgb,
                    depth_state,
                    stencil_state,
                    blend_state,
                    fragment.opacity);
            }
        }
    }
}

void validate_mesh(
    const Mesh& mesh,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    BaseColorSource source,
    const DirectionalLight& light) {
    for (const TriangleIndices& triangle : mesh.triangles) {
        for (const std::uint32_t index : triangle) {
            if (static_cast<std::size_t>(index) >= mesh.vertices.size()) {
                throw std::out_of_range("mesh triangle index out of range");
            }
        }
    }
    if (mesh.vertices.empty()) {
        return;
    }
    validate_pack(mesh.vertices.front().varyings);
    validate_output_binding(color_binding, texture_binding, source, mesh.vertices.front().varyings.count);
    if (light.enabled) {
        validate_normal_binding(light.normal, mesh.vertices.front().varyings);
    }
    for (const Vertex& vertex : mesh.vertices) {
        validate_layout_match(mesh.vertices.front().varyings, vertex.varyings);
        validate_texture_coordinates(vertex.varyings, texture_binding, source);
        if (light.enabled) {
            validate_normal_value(vertex.varyings, light.normal);
        }
    }
}

std::size_t mesh_varying_count(const Mesh& mesh) {
    return mesh.vertices.empty() ? 0U : mesh.vertices.front().varyings.count;
}

Triangle assemble_triangle(const Mesh& mesh, const TriangleIndices& indices) {
    return Triangle{mesh.vertices[indices[0]], mesh.vertices[indices[1]], mesh.vertices[indices[2]]};
}

void draw_triangle_impl(
    Framebuffer& framebuffer,
    const Triangle& triangle,
    const Mat4& mvp,
    const Mat4* light_mvp,
    const ColorBinding& color_binding,
    const TextureBinding& texture_binding,
    BaseColorSource source,
    const DirectionalLight& light,
    const MaterialState& material,
    CullMode cull_mode,
    FrontFace front_face,
    const DepthState& depth_state,
    const StencilState& stencil_state,
    const BlendState& blend_state,
    const AlphaToCoverageState& alpha_to_coverage_state,
    const AlphaTestState& alpha_test_state,
    const ShadowState& shadow_state,
    const FragmentProgram* fragment_program,
    const detail::ResolvedViewportState& viewport_state) {
    std::array<ClipVertex, 3> clip{};
    for (std::size_t i = 0; i < triangle.size(); ++i) {
        const Vec4 object_position{
            triangle[i].position.x,
            triangle[i].position.y,
            triangle[i].position.z,
            1.0F,
        };
        clip[i] = {
            mvp * object_position,
            triangle[i].varyings,
            light_mvp != nullptr ? (*light_mvp) * object_position : Vec4{},
            light_mvp != nullptr,
        };
    }
    apply_flat_provoking_vertex(clip);

    const std::vector<ClipVertex> polygon = clip_triangle(clip);
    if (polygon.size() < 3U) {
        return;
    }

    for (std::size_t i = 1; i + 1U < polygon.size(); ++i) {
        if (should_cull_projected_triangle(
                polygon[0], polygon[i], polygon[i + 1U], cull_mode, front_face)) {
            continue;
        }

        const auto a = to_screen(polygon[0], viewport_state.viewport);
        const auto b = to_screen(polygon[i], viewport_state.viewport);
        const auto c = to_screen(polygon[i + 1U], viewport_state.viewport);
        if (!a || !b || !c) {
            continue;
        }
        rasterize_screen_triangle(
            framebuffer,
            {*a, *b, *c},
            color_binding,
            texture_binding,
            source,
            light,
            material,
            depth_state,
            stencil_state,
            blend_state,
            alpha_to_coverage_state,
            alpha_test_state,
            shadow_state,
            fragment_program,
            viewport_state.scissor);
    }
}

}  // namespace

void Rasterizer::draw_triangle(const Triangle& triangle, const Mat4& model, const Mat4& view, const Mat4& projection) {
    const Triangle programmed = detail::apply_vertex_program(vertex_program_, triangle);

    detail::validate_face_culling(cull_mode_, front_face_);
    validate_depth_state(depth_state_);
    validate_stencil_state(stencil_state_);
    validate_blend_state(blend_state_);
    detail::validate_alpha_test_state(alpha_test_state_);
    validate_raster_target(framebuffer_);
    detail::validate_alpha_to_coverage_target(framebuffer_, alpha_to_coverage_state_);
    detail::validate_shadow_state_definition(shadow_state_, directional_light_.enabled);
    const detail::ResolvedViewportState viewport_state =
        detail::resolve_viewport_state(framebuffer_, viewport_state_);
    const BaseColorSource source = prepare_base_color_source(base_color_source_, texture_binding_);
    const MaterialState material = prepare_material_state(material_state_);
    const DirectionalLight light = prepare_directional_light(directional_light_);
    validate_triangle_varyings(programmed, color_binding_, texture_binding_, source, light);
    validate_fragment_program(fragment_program_, programmed[0].varyings.count);

    Triangle prepared = programmed;
    if (light.enabled) {
        prepared = transform_triangle_normals(programmed, light.normal, normal_matrix(model));
    }
    const Mat4 light_mvp = shadow_state_.enabled
        ? shadow_state_.light_view_projection * model
        : Mat4::identity();
    draw_triangle_impl(
        framebuffer_,
        prepared,
        projection * view * model,
        shadow_state_.enabled ? &light_mvp : nullptr,
        color_binding_,
        texture_binding_,
        source,
        light,
        material,
        cull_mode_,
        front_face_,
        depth_state_,
        stencil_state_,
        blend_state_,
        alpha_to_coverage_state_,
        alpha_test_state_,
        shadow_state_,
        fragment_program_.get(),
        viewport_state);
}

void Rasterizer::draw_triangle(const Triangle& triangle, const Mat4& mvp) {
    if (directional_light_.enabled || shadow_state_.enabled) {
        throw std::invalid_argument("directional lighting and shadows require separate model/view/projection transforms");
    }
    const Triangle programmed = detail::apply_vertex_program(vertex_program_, triangle);

    detail::validate_face_culling(cull_mode_, front_face_);
    validate_depth_state(depth_state_);
    validate_stencil_state(stencil_state_);
    validate_blend_state(blend_state_);
    detail::validate_alpha_test_state(alpha_test_state_);
    validate_raster_target(framebuffer_);
    detail::validate_alpha_to_coverage_target(framebuffer_, alpha_to_coverage_state_);
    detail::validate_shadow_state_definition(shadow_state_, directional_light_.enabled);
    const detail::ResolvedViewportState viewport_state =
        detail::resolve_viewport_state(framebuffer_, viewport_state_);
    const BaseColorSource source = prepare_base_color_source(base_color_source_, texture_binding_);
    const MaterialState material = prepare_material_state(material_state_);
    const DirectionalLight light = prepare_directional_light(directional_light_);
    validate_triangle_varyings(programmed, color_binding_, texture_binding_, source, light);
    validate_fragment_program(fragment_program_, programmed[0].varyings.count);
    draw_triangle_impl(
        framebuffer_,
        programmed,
        mvp,
        nullptr,
        color_binding_,
        texture_binding_,
        source,
        light,
        material,
        cull_mode_,
        front_face_,
        depth_state_,
        stencil_state_,
        blend_state_,
        alpha_to_coverage_state_,
        alpha_test_state_,
        shadow_state_,
        fragment_program_.get(),
        viewport_state);
}

void Rasterizer::draw_mesh(const Mesh& mesh, const Mat4& model, const Mat4& view, const Mat4& projection) {
    const detail::PreparedVertexMesh programmed =
        detail::prepare_vertex_program_mesh(vertex_program_, mesh);
    const Mesh& vertex_mesh = programmed.get();

    detail::validate_face_culling(cull_mode_, front_face_);
    validate_depth_state(depth_state_);
    validate_stencil_state(stencil_state_);
    validate_blend_state(blend_state_);
    detail::validate_alpha_test_state(alpha_test_state_);
    validate_raster_target(framebuffer_);
    detail::validate_alpha_to_coverage_target(framebuffer_, alpha_to_coverage_state_);
    detail::validate_shadow_state_definition(shadow_state_, directional_light_.enabled);
    const detail::ResolvedViewportState viewport_state =
        detail::resolve_viewport_state(framebuffer_, viewport_state_);
    const BaseColorSource source = prepare_base_color_source(base_color_source_, texture_binding_);
    const MaterialState material = prepare_material_state(material_state_);
    const DirectionalLight light = prepare_directional_light(directional_light_);
    validate_mesh(vertex_mesh, color_binding_, texture_binding_, source, light);
    validate_fragment_program(fragment_program_, mesh_varying_count(vertex_mesh));

    Mesh prepared = vertex_mesh;
    if (light.enabled && !vertex_mesh.vertices.empty()) {
        prepared = transform_mesh_normals(vertex_mesh, light.normal, normal_matrix(model));
    }
    const Mat4 mvp = projection * view * model;
    const Mat4 light_mvp = shadow_state_.enabled
        ? shadow_state_.light_view_projection * model
        : Mat4::identity();
    for (const TriangleIndices& indices : prepared.triangles) {
        draw_triangle_impl(
            framebuffer_,
            assemble_triangle(prepared, indices),
            mvp,
            shadow_state_.enabled ? &light_mvp : nullptr,
            color_binding_,
            texture_binding_,
            source,
            light,
            material,
            cull_mode_,
            front_face_,
            depth_state_,
            stencil_state_,
            blend_state_,
            alpha_to_coverage_state_,
            alpha_test_state_,
            shadow_state_,
            fragment_program_.get(),
            viewport_state);
    }
}

void Rasterizer::draw_mesh(const Mesh& mesh, const Mat4& mvp) {
    if (directional_light_.enabled || shadow_state_.enabled) {
        throw std::invalid_argument("directional lighting and shadows require separate model/view/projection transforms");
    }
    const detail::PreparedVertexMesh programmed =
        detail::prepare_vertex_program_mesh(vertex_program_, mesh);
    const Mesh& prepared_mesh = programmed.get();

    detail::validate_face_culling(cull_mode_, front_face_);
    validate_depth_state(depth_state_);
    validate_stencil_state(stencil_state_);
    validate_blend_state(blend_state_);
    detail::validate_alpha_test_state(alpha_test_state_);
    validate_raster_target(framebuffer_);
    detail::validate_alpha_to_coverage_target(framebuffer_, alpha_to_coverage_state_);
    detail::validate_shadow_state_definition(shadow_state_, directional_light_.enabled);
    const detail::ResolvedViewportState viewport_state =
        detail::resolve_viewport_state(framebuffer_, viewport_state_);
    const BaseColorSource source = prepare_base_color_source(base_color_source_, texture_binding_);
    const MaterialState material = prepare_material_state(material_state_);
    const DirectionalLight light = prepare_directional_light(directional_light_);
    validate_mesh(prepared_mesh, color_binding_, texture_binding_, source, light);
    validate_fragment_program(fragment_program_, mesh_varying_count(prepared_mesh));
    for (const TriangleIndices& indices : prepared_mesh.triangles) {
        draw_triangle_impl(
            framebuffer_,
            assemble_triangle(prepared_mesh, indices),
            mvp,
            nullptr,
            color_binding_,
            texture_binding_,
            source,
            light,
            material,
            cull_mode_,
            front_face_,
            depth_state_,
            stencil_state_,
            blend_state_,
            alpha_to_coverage_state_,
            alpha_test_state_,
            shadow_state_,
            fragment_program_.get(),
            viewport_state);
    }
}

}  // namespace tiny_renderer