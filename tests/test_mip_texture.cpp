#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

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

void check_near(float actual, float expected, const std::string& message, float epsilon = 1.0e-6F) {
    check(std::fabs(actual - expected) <= epsilon,
          message + " (actual=" + std::to_string(actual)
              + ", expected=" + std::to_string(expected) + ")");
}

void check_vec_near(const Vec3& actual, const Vec3& expected, const std::string& message) {
    check_near(actual.x, expected.x, message + " x");
    check_near(actual.y, expected.y, message + " y");
    check_near(actual.z, expected.z, message + " z");
}

Vec3 gray(float value) {
    return {value, value, value};
}

void test_complete_chain_and_odd_extent_rule() {
    std::vector<Vec3> texels;
    texels.reserve(9U);
    for (std::size_t index = 0U; index < 9U; ++index) {
        texels.push_back(gray(static_cast<float>(index) / 8.0F));
    }
    const Texture2D texture(3U, 3U, texels);

    check(texture.mip_level_count() == 3U, "3x3 texture owns a complete 3x3 -> 2x2 -> 1x1 chain");
    check(texture.mip_width(0U) == 3U && texture.mip_height(0U) == 3U,
          "base mip extent is preserved");
    check(texture.mip_width(1U) == 2U && texture.mip_height(1U) == 2U,
          "odd mip extent uses ceil-half dimensions");
    check(texture.mip_width(2U) == 1U && texture.mip_height(2U) == 1U,
          "mip chain terminates at 1x1");

    check_vec_near(texture.mip_texel(1U, 0U, 0U), gray(0.25F),
                   "full 2x2 parent footprint is averaged equally");
    check_vec_near(texture.mip_texel(1U, 1U, 0U), gray(0.4375F),
                   "odd right edge averages only existing parent texels");
    check_vec_near(texture.mip_texel(1U, 0U, 1U), gray(0.8125F),
                   "odd bottom edge averages only existing parent texels");
    check_vec_near(texture.mip_texel(1U, 1U, 1U), gray(1.0F),
                   "odd corner preserves its single existing parent texel");
    check_vec_near(texture.mip_texel(2U, 0U, 0U), gray(0.625F),
                   "each generated level averages the previous level deterministically");
}

void test_default_level_zero_compatibility() {
    const Texture2D texture(2U, 2U, {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {1.0F, 1.0F, 1.0F},
    });
    SamplerState sampler;
    sampler.address_u = AddressMode::Clamp;
    sampler.address_v = AddressMode::Clamp;
    sampler.filter = FilterMode::Nearest;

    const Vec2 uv{0.25F, 0.25F};
    const Vec3 legacy = texture.sample(uv, sampler);
    check_vec_near(legacy, {1.0F, 0.0F, 0.0F},
                   "default sample preserves level-zero nearest behavior");
    check_vec_near(texture.sample_lod(uv, 100.0F, sampler), legacy,
                   "disabled mip policy ignores explicit LOD and preserves level zero");

    const TextureGradients nonfinite{{std::numeric_limits<float>::quiet_NaN(), 0.0F}, {0.0F, 0.0F}};
    check_vec_near(texture.sample_grad(uv, nonfinite, sampler), legacy,
                   "disabled mip policy does not impose derivative requirements on legacy sampling");
}

void test_nearest_level_and_linear_mip_sampling() {
    const Texture2D texture(2U, 2U, {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
        {1.0F, 1.0F, 1.0F},
    });
    const Vec2 uv{0.25F, 0.25F};

    SamplerState nearest_mip;
    nearest_mip.filter = FilterMode::Nearest;
    nearest_mip.mip_filter = MipFilterMode::Nearest;
    check_vec_near(texture.sample_lod(uv, 0.49F, nearest_mip), {1.0F, 0.0F, 0.0F},
                   "nearest mip policy rounds below half to level zero");
    check_vec_near(texture.sample_lod(uv, 0.5F, nearest_mip), {0.5F, 0.5F, 0.5F},
                   "nearest mip policy rounds exact half upward to the next level");

    SamplerState linear_mip = nearest_mip;
    linear_mip.mip_filter = MipFilterMode::Linear;
    check_vec_near(texture.sample_lod(uv, 0.5F, linear_mip), {0.75F, 0.25F, 0.25F},
                   "linear mip policy interpolates deterministically between adjacent levels");
}

void test_gradient_derived_lod() {
    std::vector<Vec3> texels(64U);
    for (std::size_t y = 0U; y < 8U; ++y) {
        for (std::size_t x = 0U; x < 8U; ++x) {
            texels[y * 8U + x] = gray(static_cast<float>(x + y * 8U) / 63.0F);
        }
    }
    const Texture2D texture(8U, 8U, texels);
    SamplerState sampler;
    sampler.filter = FilterMode::Bilinear;
    sampler.mip_filter = MipFilterMode::Nearest;
    const Vec2 uv{0.37F, 0.61F};
    const TextureGradients gradients{{0.5F, 0.0F}, {0.0F, 0.125F}};

    // max texel-space footprint is 0.5 * 8 = 4, so lod = log2(4) = 2.
    check_vec_near(texture.sample_grad(uv, gradients, sampler),
                   texture.sample_lod(uv, 2.0F, sampler),
                   "gradient sampling derives the documented isotropic texel-space LOD");
}

void test_invalid_state_rejection() {
    const Texture2D texture(1U, 1U, {{0.25F, 0.5F, 0.75F}});
    SamplerState invalid;
    invalid.mip_filter = static_cast<MipFilterMode>(99);
    bool threw = false;
    try {
        (void)texture.sample_lod({0.5F, 0.5F}, 0.0F, invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "unknown mip filter mode is rejected deterministically");

    SamplerState enabled;
    enabled.mip_filter = MipFilterMode::Nearest;
    threw = false;
    try {
        (void)texture.sample_grad(
            {0.5F, 0.5F},
            {{std::numeric_limits<float>::infinity(), 0.0F}, {0.0F, 0.0F}},
            enabled);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "enabled mip sampling rejects non-finite gradients");
}

}  // namespace

int main() {
    test_complete_chain_and_odd_extent_rule();
    test_default_level_zero_compatibility();
    test_nearest_level_and_linear_mip_sampling();
    test_gradient_derived_lod();
    test_invalid_state_rejection();
    if (failures != 0) {
        std::cerr << failures << " mip texture test(s) failed\n";
        return 1;
    }
    std::cout << "mip texture tests passed\n";
    return 0;
}
