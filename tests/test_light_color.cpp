#include <cmath>
          #include <cstddef>
          #include <cstdint>
          #include <iostream>
          #include <limits>
          #include <memory>
          #include <stdexcept>
          #include <string>
          #include <vector>

          #include "tiny_renderer/framebuffer.hpp"
          #include "tiny_renderer/model_renderer.hpp"
          #include "tiny_renderer/rasterizer.hpp"
          #include "tiny_renderer/shadow.hpp"

          using namespace tiny_renderer;

          namespace {

          int failures = 0;

          void check(bool condition, const std::string& message) {
              if (!condition) {
                  ++failures;
                  std::cerr << "FAIL: " << message << '\n';
              }
          }

          void check_near(float actual, float expected, const std::string& message, float epsilon = 1.5e-2F) {
              check(
                  std::fabs(actual - expected) <= epsilon,
                  message + " (actual=" + std::to_string(actual)
                      + ", expected=" + std::to_string(expected) + ")");
          }

          Vertex normal_vertex(const Vec3& position) {
              return Vertex::with_varyings(position, VaryingPack{0.0F, 0.0F, 1.0F});
          }

          ModelAsset model(const MaterialState& material) {
              ModelAsset asset;
              asset.mesh.vertices = {
                  normal_vertex({-0.75F, -0.75F, 0.0F}),
                  normal_vertex({0.75F, -0.75F, 0.0F}),
                  normal_vertex({0.0F, 0.75F, 0.0F}),
              };
              asset.mesh.triangles = {{0U, 1U, 2U}};
              MaterialDraw draw;
              draw.range = {0U, 1U};
              draw.material_name = "light-color";
              draw.material = material;
              asset.draws.push_back(draw);
              return asset;
          }

          MaterialState diffuse_material() {
              MaterialState material;
              material.albedo = {1.0F, 1.0F, 1.0F};
              material.specular = {0.0F, 0.0F, 0.0F};
              return material;
          }

          MaterialState specular_only_material() {
              MaterialState material;
              material.albedo = {0.0F, 0.0F, 0.0F};
              material.specular = {1.0F, 1.0F, 1.0F};
              material.shininess = 16.0F;
              return material;
          }

          DirectionalLight directional(float ambient = 0.0F, float diffuse = 1.0F) {
              DirectionalLight light;
              light.enabled = true;
              light.normal = {0U, 1U, 2U};
              light.direction_to_light = {0.0F, 0.0F, 1.0F};
              light.ambient = ambient;
              light.diffuse = diffuse;
              light.viewer_position = {0.0F, 0.0F, 4.0F};
              return light;
          }

          PointLight point(float ambient = 0.0F, float diffuse = 1.0F) {
              PointLight light;
              light.enabled = true;
              light.normal = {0U, 1U, 2U};
              light.position = {0.0F, 0.0F, 2.0F};
              light.ambient = ambient;
              light.diffuse = diffuse;
              light.viewer_position = {0.0F, 0.0F, 4.0F};
              return light;
          }

          SpotLight spot(float ambient = 0.0F, float diffuse = 1.0F) {
              SpotLight light;
              light.enabled = true;
              light.normal = {0U, 1U, 2U};
              light.position = {0.0F, 0.0F, 2.0F};
              light.direction = {0.0F, 0.0F, -1.0F};
              light.ambient = ambient;
              light.diffuse = diffuse;
              light.viewer_position = {0.0F, 0.0F, 4.0F};
              light.inner_cone_cos = 0.9F;
              light.outer_cone_cos = 0.8F;
              return light;
          }

          FixedLight fixed_directional(const DirectionalLight& light) {
              FixedLight result;
              result.type = FixedLightType::Directional;
              result.directional = light;
              return result;
          }

          FixedLight fixed_point(const PointLight& light) {
              FixedLight result;
              result.type = FixedLightType::Point;
              result.point = light;
              return result;
          }

          FixedLight fixed_spot(const SpotLight& light) {
              FixedLight result;
              result.type = FixedLightType::Spot;
              result.spot = light;
              return result;
          }

          FixedLightCollection collection(std::initializer_list<FixedLight> lights) {
              FixedLightCollection result;
              result.count = lights.size();
              std::size_t i = 0U;
              for (const FixedLight& light : lights) {
                  result.lights[i++] = light;
              }
              return result;
          }

          Framebuffer render(const ModelRenderOptions& options, const MaterialState& material = diffuse_material()) {
              Framebuffer framebuffer(65U, 65U);
              draw_model_asset(
                  framebuffer,
                  model(material),
                  Mat4::identity(), Mat4::identity(), Mat4::identity(),
                  options);
              return framebuffer;
          }

          void check_rgb(const Vec3& actual, const Vec3& expected, const std::string& label) {
              check_near(actual.x, expected.x, label + " red");
              check_near(actual.y, expected.y, label + " green");
              check_near(actual.z, expected.z, label + " blue");
          }

          void test_white_defaults_are_compatible() {
              ModelRenderOptions default_directional;
              default_directional.directional_light = directional(0.15F, 0.6F);
              ModelRenderOptions white_directional = default_directional;
              white_directional.directional_light.color = {1.0F, 1.0F, 1.0F};
              const Framebuffer a = render(default_directional);
              const Framebuffer b = render(white_directional);
              check(a.rgb8() == b.rgb8(), "explicit white directional color preserves legacy bytes");
              check(a.fnv1a64() == b.fnv1a64(), "explicit white directional color preserves legacy hash");

              ModelRenderOptions default_point;
              default_point.point_light = point(0.1F, 0.5F);
              ModelRenderOptions white_point = default_point;
              white_point.point_light.color = {1.0F, 1.0F, 1.0F};
              check(render(default_point).rgb8() == render(white_point).rgb8(),
                    "explicit white point color preserves legacy bytes");
          }

          void test_directional_color_tints_ambient_diffuse_and_specular() {
              DirectionalLight light = directional(0.2F, 0.3F);
              light.color = {0.8F, 0.4F, 0.2F};
              ModelRenderOptions options;
              options.directional_light = light;
              check_rgb(
                  render(options).color_at(32U, 32U),
                  {0.4F, 0.2F, 0.1F},
                  "directional color multiplies ambient plus Lambert diffuse");

              DirectionalLight white = directional(0.0F, 1.0F);
              ModelRenderOptions white_options;
              white_options.directional_light = white;
              DirectionalLight tinted = white;
              tinted.color = {0.25F, 0.5F, 1.0F};
              ModelRenderOptions tinted_options;
              tinted_options.directional_light = tinted;
              const Vec3 reference = render(white_options, specular_only_material()).color_at(32U, 32U);
              const Vec3 actual = render(tinted_options, specular_only_material()).color_at(32U, 32U);
              check_rgb(
                  actual,
                  {reference.x * 0.25F, reference.y * 0.5F, reference.z},
                  "directional color multiplies Blinn-Phong specular");
          }

          void test_point_and_spot_colors_share_the_rule() {
              PointLight white_point = point();
              ModelRenderOptions p_white;
              p_white.point_light = white_point;
              PointLight tinted_point = white_point;
              tinted_point.color = {1.0F, 0.25F, 0.5F};
              ModelRenderOptions p_tinted;
              p_tinted.point_light = tinted_point;
              const Vec3 point_reference = render(p_white).color_at(32U, 32U);
              const Vec3 point_actual = render(p_tinted).color_at(32U, 32U);
              check_rgb(
                  point_actual,
                  {point_reference.x, point_reference.y * 0.25F, point_reference.z * 0.5F},
                  "point-light color uses the common contribution path");

              SpotLight white_spot = spot();
              ModelRenderOptions s_white;
              s_white.fixed_lights = collection({fixed_spot(white_spot)});
              SpotLight tinted_spot = white_spot;
              tinted_spot.color = {0.5F, 1.0F, 0.25F};
              ModelRenderOptions s_tinted;
              s_tinted.fixed_lights = collection({fixed_spot(tinted_spot)});
              const Vec3 spot_reference = render(s_white).color_at(32U, 32U);
              const Vec3 spot_actual = render(s_tinted).color_at(32U, 32U);
              check_rgb(
                  spot_actual,
                  {spot_reference.x * 0.5F, spot_reference.y, spot_reference.z * 0.25F},
                  "spotlight color uses the common contribution path");
          }

          void test_mixed_colored_lights_accumulate_in_caller_order() {
              DirectionalLight red = directional(0.25F, 0.0F);
              red.color = {1.0F, 0.0F, 0.0F};
              PointLight green = point(0.25F, 0.0F);
              green.color = {0.0F, 1.0F, 0.0F};
              SpotLight blue = spot(0.25F, 0.0F);
              blue.color = {0.0F, 0.0F, 1.0F};
              ModelRenderOptions options;
              options.fixed_lights = collection({
                  fixed_directional(red), fixed_point(green), fixed_spot(blue),
              });
              check_rgb(
                  render(options).color_at(32U, 32U),
                  {0.25F, 0.25F, 0.25F},
                  "caller-ordered colored ambient contributions accumulate component-wise");
          }

          void test_shadow_preserves_colored_ambient_and_other_lights() {
              DirectionalLight red = directional(0.1F, 0.4F);
              red.color = {1.0F, 0.0F, 0.0F};
              PointLight green = point(0.0F, 0.25F);
              green.color = {0.0F, 1.0F, 0.0F};

              ModelRenderOptions options;
              options.fixed_lights = collection({fixed_directional(red), fixed_point(green)});
              options.fixed_lights.shadowed_directional_index = 0U;
              options.shadow_state.enabled = true;
              options.shadow_state.map = std::make_shared<const DepthTexture2D>(
                  1U, 1U, std::vector<float>{0.0F});
              options.shadow_state.light_view_projection = Mat4::identity();

              check_rgb(
                  render(options).color_at(32U, 32U),
                  {0.1F, 0.25F, 0.0F},
                  "shadow suppresses only colored direct light while ambient and unrelated light remain");
          }

          class RotateRgb final : public FragmentProgram {
          public:
              FragmentProgramOutput shade(const FragmentProgramInput& input) const noexcept override {
                  return {
                      {input.fixed_rgb.y, input.fixed_rgb.z, input.fixed_rgb.x},
                      input.fixed_opacity,
                      false,
                  };
              }
          };

          void test_fragment_program_observes_completed_colored_lighting() {
              DirectionalLight red = directional(0.5F, 0.0F);
              red.color = {1.0F, 0.0F, 0.0F};
              ModelRenderOptions options;
              options.fixed_lights = collection({fixed_directional(red)});
              options.fragment_program = std::make_shared<const RotateRgb>();
              check_rgb(
                  render(options).color_at(32U, 32U),
                  {0.0F, 0.0F, 0.5F},
                  "fragment program runs after completed colored fixed lighting");
          }

          void test_invalid_colors_fail_closed() {
              const ModelAsset asset = model(diffuse_material());
              {
                  ModelRenderOptions options;
                  options.directional_light = directional();
                  options.directional_light.color = {
                      std::numeric_limits<float>::quiet_NaN(), 1.0F, 1.0F};
                  bool threw = false;
                  try {
                      (void)prepare_model_asset(asset, options);
                  } catch (const std::invalid_argument&) {
                      threw = true;
                  }
                  check(threw, "non-finite directional color is rejected during preparation");
              }
              {
                  ModelRenderOptions options;
                  options.point_light = point();
                  options.point_light.color = {-0.1F, 0.5F, 0.5F};
                  bool threw = false;
                  try {
                      (void)prepare_model_asset(asset, options);
                  } catch (const std::invalid_argument&) {
                      threw = true;
                  }
                  check(threw, "negative point-light color is rejected during preparation");
              }
              {
                  SpotLight invalid = spot();
                  invalid.color = {0.5F, 0.5F, 1.1F};
                  ModelRenderOptions options;
                  options.fixed_lights = collection({fixed_spot(invalid)});
                  Framebuffer framebuffer(65U, 65U);
                  framebuffer.clear({0.2F, 0.3F, 0.4F}, 0.8F, 17U);
                  const auto before = framebuffer.rgb8();
                  const float before_depth = framebuffer.depth_at(32U, 32U);
                  const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);
                  bool threw = false;
                  try {
                      draw_model_asset(
                          framebuffer, asset,
                          Mat4::identity(), Mat4::identity(), Mat4::identity(),
                          options);
                  } catch (const std::invalid_argument&) {
                      threw = true;
                  }
                  check(threw, "out-of-range spotlight color rejects direct model submission");
                  check(framebuffer.rgb8() == before, "invalid color leaves framebuffer RGB untouched");
                  check(framebuffer.depth_at(32U, 32U) == before_depth,
                        "invalid color leaves framebuffer depth untouched");
                  check(framebuffer.stencil_at(32U, 32U) == before_stencil,
                        "invalid color leaves framebuffer stencil untouched");
              }
          }

          void test_prepared_list_preserves_colored_state() {
              DirectionalLight red = directional(0.15F, 0.35F);
              red.color = {1.0F, 0.25F, 0.25F};
              PointLight blue = point(0.05F, 0.2F);
              blue.color = {0.25F, 0.25F, 1.0F};
              ModelRenderOptions options;
              options.fixed_lights = collection({fixed_directional(red), fixed_point(blue)});
              const ModelAsset asset = model(diffuse_material());
              const PreparedModelSubmission prepared = prepare_model_asset(asset, options);

              Framebuffer direct(65U, 65U);
              draw_model_asset(
                  direct, asset,
                  Mat4::identity(), Mat4::identity(), Mat4::identity(), options);
              Framebuffer listed(65U, 65U);
              const PreparedModelListEntry entries[] = {{&prepared, Mat4::identity()}};
              draw_prepared_model_list(listed, entries, Mat4::identity(), Mat4::identity());
              check(listed.rgb8() == direct.rgb8(),
                    "prepared list preserves colored fixed-light bytes");
              check(listed.fnv1a64() == direct.fnv1a64(),
                    "prepared list preserves colored fixed-light hash");
          }

          }  // namespace

          int main() {
              try {
                  test_white_defaults_are_compatible();
                  test_directional_color_tints_ambient_diffuse_and_specular();
                  test_point_and_spot_colors_share_the_rule();
                  test_mixed_colored_lights_accumulate_in_caller_order();
                  test_shadow_preserves_colored_ambient_and_other_lights();
                  test_fragment_program_observes_completed_colored_lighting();
                  test_invalid_colors_fail_closed();
                  test_prepared_list_preserves_colored_state();
              } catch (const std::exception& error) {
                  std::cerr << "unexpected exception: " << error.what() << '\n';
                  return 2;
              }
              if (failures != 0) {
                  std::cerr << failures << " light-color test(s) failed\n";
                  return 1;
              }
              std::cout << "all light-color tests passed\n";
              return 0;
          }
