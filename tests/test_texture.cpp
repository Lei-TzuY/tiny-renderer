#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/mesh.hpp"
#include "tiny_renderer/rasterizer.hpp"
#include "tiny_renderer/texture.hpp"

using namespace tiny_renderer;

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void check_near(float actual, float expected, const std::string& message, float epsilon = 2.0e-4F) {
    check(std::fabs(actual - expected) <= epsilon,
          message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
}

void check_color(const Vec3& actual, const Vec3& expected, const std::string& message, float epsilon = 2.0e-4F) {
    check_near(actual.x, expected.x, message + " red", epsilon);
    check_near(actual.y, expected.y, message + " green", epsilon);
    check_near(actual.z, expected.z, message + " blue", epsilon);
}

std::size_t count_non_black(const Framebuffer& fb) {
    std::size_t count = 0U;
    for (std::size_t y = 0; y < fb.height(); ++y) {
        for (std::size_t x = 0; x < fb.width(); ++x) {
            const Vec3& color = fb.color_at(x, y);
            if (color.x != 0.0F || color.y != 0.0F || color.z != 0.0F) {
                ++count;
            }
        }
    }
    return count;
}

Mat4 projective_w_from_z() {
    Mat4 projective{};
    projective(0, 0) = 1.0F;
    projective(1, 1) = 1.0F;
    projective(3, 2) = 1.0F;
    return projective;
}

Vertex uv_vertex(const Vec3& position, float u, float v) {
    return Vertex::with_varyings(position, VaryingPack{u, v});
}

Vertex interpolate_smooth_uv_vertex(const Vertex& a, const Vertex& b, float t) {
    const Vec3 position = a.position + (b.position - a.position) * t;
    const float u = a.varyings.values[0] + (b.varyings.values[0] - a.varyings.values[0]) * t;
    const float v = a.varyings.values[1] + (b.varyings.values[1] - a.varyings.values[1]) * t;
    return uv_vertex(position, u, v);
}

float left_clip_distance_for_projective_fixture(const Vertex& vertex) {
    // projective_w_from_z maps object (x, y, z) to clip (x, y, 0, z),
    // so the canonical left-plane distance w + x is z + x.
    return vertex.position.z + vertex.position.x;
}

TextureBinding texture_binding(
    const Texture2D& texture,
    FilterMode filter = FilterMode::Nearest,
    AddressMode address_u = AddressMode::Clamp,
    AddressMode address_v = AddressMode::Clamp,
    MipFilterMode mip_filter = MipFilterMode::Disabled) {
    return TextureBinding{&texture, 0U, 1U, SamplerState{address_u, address_v, filter, mip_filter}};
}

void test_sampler_address_and_filter_modes() {
    const Vec3 red{1.0F, 0.0F, 0.0F};
    const Vec3 green{0.0F, 1.0F, 0.0F};
    const Vec3 blue{0.0F, 0.0F, 1.0F};
    const Vec3 white{1.0F, 1.0F, 1.0F};
    const Texture2D texture(2U, 2U, {red, green, blue, white});

    check_color(texture.sample({0.25F, 0.25F}), red, "nearest top-left texel");
    check_color(texture.sample({0.75F, 0.25F}), green, "nearest top-right texel");
    check_color(texture.sample({0.25F, 0.75F}), blue, "nearest bottom-left texel");
    check_color(texture.sample({0.75F, 0.75F}), white, "nearest bottom-right texel");

    const SamplerState clamp_nearest{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Nearest};
    check_color(texture.sample({-2.0F, 0.25F}, clamp_nearest), red, "clamp address mode clamps outside U");

    const SamplerState repeat_nearest{AddressMode::Repeat, AddressMode::Clamp, FilterMode::Nearest};
    check_color(texture.sample({-0.25F, 0.25F}, repeat_nearest), green, "repeat address mode wraps negative U");

    const SamplerState clamp_bilinear{AddressMode::Clamp, AddressMode::Clamp, FilterMode::Bilinear};
    check_color(texture.sample({0.5F, 0.5F}, clamp_bilinear), {0.5F, 0.5F, 0.5F}, "bilinear center averages four texels");
    check_color(texture.sample({0.0F, 0.0F}, clamp_bilinear), red, "bilinear clamp pins texture corner");

    const SamplerState repeat_bilinear{AddressMode::Repeat, AddressMode::Clamp, FilterMode::Bilinear};
    check_color(texture.sample({0.0F, 0.25F}, repeat_bilinear), {0.5F, 0.5F, 0.0F}, "bilinear repeat filters across U seam");
}

void test_mip_chain_and_explicit_lod_sampling() {
    std::vector<Vec3> texels;
    texels.reserve(9U);
    for (std::size_t index = 0U; index < 9U; ++index) {
        const float value = static_cast<float>(index) / 8.0F;
        texels.push_back({value, value, value});
    }
    const Texture2D odd(3U, 3U, texels);
    check(odd.mip_level_count() == 3U, "3x3 texture owns complete 3x3 -> 2x2 -> 1x1 mip chain");
    check(odd.mip_width(1U) == 2U && odd.mip_height(1U) == 2U,
          "odd mip extent uses deterministic ceil-half dimensions");
    check_color(odd.mip_texel(1U, 0U, 0U), {0.25F, 0.25F, 0.25F},
                "full 2x2 parent footprint averages equally");
    check_color(odd.mip_texel(1U, 1U, 0U), {0.4375F, 0.4375F, 0.4375F},
                "odd right edge averages only existing parent texels");
    check_color(odd.mip_texel(1U, 0U, 1U), {0.8125F, 0.8125F, 0.8125F},
                "odd bottom edge averages only existing parent texels");
    check_color(odd.mip_texel(1U, 1U, 1U), {1.0F, 1.0F, 1.0F},
                "odd corner preserves its single parent texel");
    check_color(odd.mip_texel(2U, 0U, 0U), {0.625F, 0.625F, 0.625F},
                "mip generation recursively averages the previous level");

    const Texture2D texture(2U, 2U, {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {1.0F, 1.0F, 1.0F},
    });
    const Vec2 uv{0.25F, 0.25F};
    SamplerState legacy;
    legacy.filter = FilterMode::Nearest;
    const Vec3 level_zero = texture.sample(uv, legacy);
    check_color(texture.sample_lod(uv, 100.0F, legacy), level_zero,
                "disabled mip policy preserves level-zero result for any explicit LOD");

    SamplerState nearest = legacy;
    nearest.mip_filter = MipFilterMode::Nearest;
    check_color(texture.sample_lod(uv, 0.49F, nearest), {1.0F, 0.0F, 0.0F},
                "nearest mip rounds below half to level zero");
    check_color(texture.sample_lod(uv, 0.5F, nearest), {0.5F, 0.5F, 0.5F},
                "nearest mip rounds exact half upward");

    SamplerState linear = nearest;
    linear.mip_filter = MipFilterMode::Linear;
    check_color(texture.sample_lod(uv, 0.5F, linear), {0.75F, 0.25F, 0.25F},
                "linear mip policy interpolates adjacent level samples");
}

void test_gradient_lod_and_invalid_mip_state() {
    std::vector<Vec3> texels(64U);
    for (std::size_t y = 0U; y < 8U; ++y) {
        for (std::size_t x = 0U; x < 8U; ++x) {
            const float value = static_cast<float>(x + y * 8U) / 63.0F;
            texels[y * 8U + x] = {value, value, value};
        }
    }
    const Texture2D texture(8U, 8U, texels);
    SamplerState sampler;
    sampler.filter = FilterMode::Bilinear;
    sampler.mip_filter = MipFilterMode::Nearest;
    const Vec2 uv{0.37F, 0.61F};
    const TextureGradients gradients{{0.5F, 0.0F}, {0.0F, 0.125F}};
    check_color(texture.sample_grad(uv, gradients, sampler), texture.sample_lod(uv, 2.0F, sampler),
                "gradient sampling uses max texel footprint and log2 LOD");

    SamplerState disabled;
    disabled.filter = FilterMode::Nearest;
    const TextureGradients nonfinite{{std::numeric_limits<float>::quiet_NaN(), 0.0F}, {0.0F, 0.0F}};
    check_color(texture.sample_grad({0.1F, 0.1F}, nonfinite, disabled),
                texture.sample({0.1F, 0.1F}, disabled),
                "mip-disabled sampling does not impose derivative requirements");

    SamplerState invalid;
    invalid.mip_filter = static_cast<MipFilterMode>(99);
    bool threw = false;
    try {
        (void)texture.sample_lod(uv, 0.0F, invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "unknown mip filter mode is rejected deterministically");
}

void test_perspective_correct_uv_sampling() {
    const Vec3 red{1.0F, 0.0F, 0.0F};
    const Vec3 green{0.0F, 1.0F, 0.0F};
    const Texture2D texture(2U, 1U, {red, green});
    const Triangle triangle{{
        uv_vertex({-0.5F, -0.5F, 1.0F}, 0.0F, 0.5F),
        uv_vertex({1.0F, -1.0F, 2.0F}, 1.5F, 0.5F),
        uv_vertex({-2.0F, 2.0F, 4.0F}, 0.0F, 0.5F),
    }};

    Framebuffer fb(33, 33);
    Rasterizer rasterizer(fb, {}, texture_binding(texture));
    rasterizer.draw_triangle(triangle, projective_w_from_z());

    const auto bary = barycentric_coordinates({8.0F, 24.0F}, {24.0F, 24.0F}, {8.0F, 8.0F}, {13.5F, 18.5F});
    check(bary.has_value(), "texture probe has barycentric coordinates");
    if (!bary) {
        return;
    }
    const float reciprocal_w = bary->x / 1.0F + bary->y / 2.0F + bary->z / 4.0F;
    const float perspective_u = (bary->y * 1.5F / 2.0F) / reciprocal_w;
    const float affine_u = bary->y * 1.5F;
    check(perspective_u < 0.5F && affine_u > 0.5F,
          "fixture separates perspective-correct and affine nearest-texel decisions");

    const Vec3& actual = fb.color_at(13U, 18U);
    check_color(actual, red, "textured fragment uses perspective-correct UV");
}

void test_textured_clipping_matches_manual_geometry() {
    const Texture2D texture(2U, 1U, {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
    });
    const TextureBinding binding = texture_binding(texture, FilterMode::Bilinear);
    const Mat4 projective = projective_w_from_z();

    const Triangle crossing{{
        uv_vertex({-2.0F, 0.0F, 1.0F}, 0.0F, 0.5F),
        uv_vertex({0.0F, -1.0F, 2.0F}, 1.0F, 0.5F),
        uv_vertex({0.0F, 2.0F, 4.0F}, 0.0F, 0.5F),
    }};

    const float distance_a = left_clip_distance_for_projective_fixture(crossing[0]);
    const float distance_b = left_clip_distance_for_projective_fixture(crossing[1]);
    const float distance_c = left_clip_distance_for_projective_fixture(crossing[2]);
    const float t_ca = distance_c / (distance_c - distance_a);
    const float t_ab = distance_a / (distance_a - distance_b);
    const Vertex intersection_ca = interpolate_smooth_uv_vertex(crossing[2], crossing[0], t_ca);
    const Vertex intersection_ab = interpolate_smooth_uv_vertex(crossing[0], crossing[1], t_ab);
    const Triangle manual_a{{intersection_ca, intersection_ab, crossing[1]}};
    const Triangle manual_b{{intersection_ca, crossing[1], crossing[2]}};

    Framebuffer automatic(65, 65);
    Rasterizer automatic_rasterizer(automatic, {}, binding);
    automatic_rasterizer.draw_triangle(crossing, projective);

    Framebuffer manual(65, 65);
    Rasterizer manual_rasterizer(manual, {}, binding);
    manual_rasterizer.draw_triangle(manual_a, projective);
    manual_rasterizer.draw_triangle(manual_b, projective);

    check(count_non_black(automatic) > 0U, "textured clipping fixture produces fragments");
    check(automatic.rgb8() == manual.rgb8(), "textured clipping matches equivalent manually clipped geometry");
}

void test_invalid_uv_binding_is_fail_closed() {
    const Texture2D texture(1U, 1U, {{1.0F, 1.0F, 1.0F}});
    const Triangle triangle{{
        uv_vertex({-0.5F, -0.5F, 0.0F}, 0.0F, 0.0F),
        uv_vertex({0.5F, -0.5F, 0.0F}, 1.0F, 0.0F),
        uv_vertex({0.0F, 0.5F, 0.0F}, 0.5F, 1.0F),
    }};

    TextureBinding invalid{&texture, 2U, 1U, {}};
    Framebuffer fb(33, 33);
    Rasterizer rasterizer(fb, {}, invalid);
    const std::uint64_t before = fb.fnv1a64();
    bool threw = false;
    try {
        rasterizer.draw_triangle(triangle, Mat4::identity());
    } catch (const std::out_of_range&) {
        threw = true;
    }

    check(threw, "out-of-range UV channel binding is rejected");
    check(fb.fnv1a64() == before, "invalid UV binding is rejected before framebuffer mutation");
}

}  // namespace

int main() {
    try {
        test_sampler_address_and_filter_modes();
        test_mip_chain_and_explicit_lod_sampling();
        test_gradient_lod_and_invalid_mip_state();
        test_perspective_correct_uv_sampling();
        test_textured_clipping_matches_manual_geometry();
        test_invalid_uv_binding_is_fail_closed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " texture test(s) failed\n";
        return 1;
    }
    std::cout << "all texture tests passed\n";
    return 0;
}
