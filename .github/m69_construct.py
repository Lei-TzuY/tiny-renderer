from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))


# Public binding: trailing field preserves existing aggregate initialization.
replace_once(
    "include/tiny_renderer/rasterizer.hpp",
    "    const Texture2D* normal_texture{nullptr};\n};",
    "    const Texture2D* normal_texture{nullptr};\n"
    "    // Optional linear-RGB specular-reflectance multiplier. It shares the\n"
    "    // material UV/sampler path with the other texture roles.\n"
    "    const Texture2D* specular_texture{nullptr};\n};")

# Model/prepared/list propagation and static sampler validation.
replace_once(
    "src/model_renderer.cpp",
    "            || static_cast<bool>(draw.opacity_texture)\n"
    "            || static_cast<bool>(draw.normal_texture);",
    "            || static_cast<bool>(draw.opacity_texture)\n"
    "            || static_cast<bool>(draw.normal_texture)\n"
    "            || static_cast<bool>(draw.specular_texture);")
replace_once(
    "src/model_renderer.cpp",
    "        if (draw.normal_texture) {\n"
    "            normal_map_present = true;",
    "        if (draw.specular_texture && !draw.specular_texture->texels_within_unit_range()) {\n"
    "            throw std::invalid_argument(\n"
    "                \"model specular texture texels must be finite and within [0, 1]\");\n"
    "        }\n"
    "        if (draw.normal_texture) {\n"
    "            normal_map_present = true;")
replace_once(
    "src/model_renderer.cpp",
    "    binding.normal_texture = draw.normal_texture.get();\n    return binding;",
    "    binding.normal_texture = draw.normal_texture.get();\n"
    "    binding.specular_texture = draw.specular_texture.get();\n"
    "    return binding;")

# Shared range/model preflight treats map_Ks as a first-class UV/sampler role.
replace_once(
    "src/rasterizer_validation.cpp",
    "    return source == BaseColorSource::Texture\n"
    "        || texture_binding.opacity_texture != nullptr\n"
    "        || texture_binding.normal_texture != nullptr;",
    "    return source == BaseColorSource::Texture\n"
    "        || texture_binding.opacity_texture != nullptr\n"
    "        || texture_binding.normal_texture != nullptr\n"
    "        || texture_binding.specular_texture != nullptr;")
replace_once(
    "src/rasterizer_validation.cpp",
    "void validate_output_binding(\n"
    "    const ColorBinding& color_binding,\n"
    "    const TextureBinding& texture_binding,\n"
    "    BaseColorSource source,\n"
    "    std::size_t varying_count) {",
    "void validate_output_binding(\n"
    "    const ColorBinding& color_binding,\n"
    "    const TextureBinding& texture_binding,\n"
    "    BaseColorSource source,\n"
    "    std::size_t varying_count) {\n"
    "    if (texture_binding.specular_texture != nullptr\n"
    "        && !texture_binding.specular_texture->texels_within_unit_range()) {\n"
    "        throw std::invalid_argument(\n"
    "            \"specular texture texels must be finite and within [0, 1]\");\n"
    "    }")

# Direct triangle/mesh path validation and gradient ownership.
replace_once(
    "src/rasterizer.cpp",
    "    if (binding.texture == nullptr\n"
    "        && binding.opacity_texture == nullptr\n"
    "        && binding.normal_texture == nullptr) {",
    "    if (binding.texture == nullptr\n"
    "        && binding.opacity_texture == nullptr\n"
    "        && binding.normal_texture == nullptr\n"
    "        && binding.specular_texture == nullptr) {")
replace_once(
    "src/rasterizer.cpp",
    "    validate_sampler_state(binding.sampler);\n"
    "    if (binding.u_channel >= varying_count || binding.v_channel >= varying_count) {",
    "    validate_sampler_state(binding.sampler);\n"
    "    if (binding.specular_texture != nullptr\n"
    "        && !binding.specular_texture->texels_within_unit_range()) {\n"
    "        throw std::invalid_argument(\n"
    "            \"specular texture texels must be finite and within [0, 1]\");\n"
    "    }\n"
    "    if (binding.u_channel >= varying_count || binding.v_channel >= varying_count) {")
replace_once(
    "src/rasterizer.cpp",
    "    if (source == BaseColorSource::Texture\n"
    "        || texture_binding.opacity_texture != nullptr\n"
    "        || texture_binding.normal_texture != nullptr) {",
    "    if (source == BaseColorSource::Texture\n"
    "        || texture_binding.opacity_texture != nullptr\n"
    "        || texture_binding.normal_texture != nullptr\n"
    "        || texture_binding.specular_texture != nullptr) {")
replace_once(
    "src/rasterizer.cpp",
    "    if (source != BaseColorSource::Texture\n"
    "        && binding.opacity_texture == nullptr\n"
    "        && binding.normal_texture == nullptr) {",
    "    if (source != BaseColorSource::Texture\n"
    "        && binding.opacity_texture == nullptr\n"
    "        && binding.normal_texture == nullptr\n"
    "        && binding.specular_texture == nullptr) {")
replace_once(
    "src/rasterizer.cpp",
    "        || (binding.texture == nullptr\n"
    "            && binding.opacity_texture == nullptr\n"
    "            && binding.normal_texture == nullptr)) {",
    "        || (binding.texture == nullptr\n"
    "            && binding.opacity_texture == nullptr\n"
    "            && binding.normal_texture == nullptr\n"
    "            && binding.specular_texture == nullptr)) {")
replace_once(
    "src/rasterizer.cpp",
    "bool material_has_specular(const MaterialState& material) {\n"
    "    return material.specular.x > 0.0F || material.specular.y > 0.0F || material.specular.z > 0.0F;\n"
    "}\n\n",
    "")

p = Path("src/rasterizer.cpp")
text = p.read_text()
marker = "\nfloat shadow_visibility(const ShadowState& shadow, const Vec4& light_clip) {"
helper = r'''
Vec3 fragment_specular_reflectance(
    const VaryingPack& varyings,
    const TextureGradients& gradients,
    const TextureBinding& texture_binding,
    const MaterialState& material) {
    if (texture_binding.specular_texture == nullptr) {
        return material.specular;
    }
    const Vec3 sampled = texture_binding.specular_texture->sample_grad(
        {varyings.values[texture_binding.u_channel], varyings.values[texture_binding.v_channel]},
        gradients,
        texture_binding.sampler);
    if (!finite_vec3(sampled)
        || sampled.x < 0.0F || sampled.x > 1.0F
        || sampled.y < 0.0F || sampled.y > 1.0F
        || sampled.z < 0.0F || sampled.z > 1.0F) {
        throw std::logic_error("validated specular texture produced an invalid sample");
    }
    return modulate_rgb(material.specular, sampled);
}

bool has_specular_reflectance(const Vec3& specular) {
    return specular.x > 0.0F || specular.y > 0.0F || specular.z > 0.0F;
}
'''
if text.count(marker) != 1:
    raise SystemExit("rasterizer.cpp: specular helper insertion marker mismatch")
text = text.replace(marker, "\n" + helper.strip() + marker, 1)

old_signature = '''Vec3 light_contribution(
    const Vec3& base,
    const Vec3& normal,
    const MaterialState& material,
    const DirectionalLight* directional_light,'''
new_signature = '''Vec3 light_contribution(
    const Vec3& base,
    const Vec3& normal,
    const MaterialState& material,
    const Vec3& specular_reflectance,
    const DirectionalLight* directional_light,'''
if text.count(old_signature) != 1:
    raise SystemExit("rasterizer.cpp: light_contribution signature mismatch")
text = text.replace(old_signature, new_signature, 1)
light_start = text.index("Vec3 light_contribution(")
light_end = text.index("\nVec3 environment_diffuse_contribution(", light_start)
light = text[light_start:light_end]
light = light.replace(
    "if (material_has_specular(material)) {",
    "if (has_specular_reflectance(specular_reflectance)) {")
light = light.replace("material.specular.x", "specular_reflectance.x")
light = light.replace("material.specular.y", "specular_reflectance.y")
light = light.replace("material.specular.z", "specular_reflectance.z")
text = text[:light_start] + light + text[light_end:]

env_start = text.index("Vec3 environment_reflection_contribution(")
env_end = text.index("\nbool reflection_is_only_zero_contribution(", env_start)
env = text[env_start:env_end]
env = env.replace(
    "    const MaterialState& material,\n",
    "    const Vec3& specular_reflectance,\n")
env = env.replace(
    "if (!fixed_lights.environment_reflection || !material_has_specular(material))",
    "if (!fixed_lights.environment_reflection || !has_specular_reflectance(specular_reflectance))")
env = env.replace(
    "return modulate_rgb(material.specular, radiance);",
    "return modulate_rgb(specular_reflectance, radiance);")
text = text[:env_start] + env + text[env_end:]

zero_start = text.index("bool reflection_is_only_zero_contribution(")
zero_end = text.index("\nShadedFragment shade_fragment(", zero_start)
zero = text[zero_start:zero_end]
zero = zero.replace(
    "    const MaterialState& material) {",
    "    const Vec3& specular_reflectance) {")
zero = zero.replace(
    "&& !material_has_specular(material)",
    "&& !has_specular_reflectance(specular_reflectance)")
text = text[:zero_start] + zero + text[zero_end:]

shade_start = text.index("ShadedFragment shade_fragment(")
shade_end = text.index("\nvoid rasterize_screen_triangle(", shade_start)
shade = text[shade_start:shade_end]
opacity_anchor = "    const float opacity = fragment_opacity(varyings, gradients, texture_binding, material);\n"
if shade.count(opacity_anchor) != 1:
    raise SystemExit("rasterizer.cpp: opacity anchor mismatch")
shade = shade.replace(
    opacity_anchor,
    opacity_anchor
    + "    // Resolve map_Ks exactly once; direct and environment specular share this value.\n"
    + "    const Vec3 specular_reflectance = fragment_specular_reflectance(\n"
    + "        varyings, gradients, texture_binding, material);\n",
    1)
shade = shade.replace(
    "directional_light, point_light, fixed_lights, material))",
    "directional_light, point_light, fixed_lights, specular_reflectance))")
shade = shade.replace(
    "base, normal, material, &directional_light, nullptr, nullptr, visibility, world_position",
    "base, normal, material, specular_reflectance, &directional_light, nullptr, nullptr, visibility, world_position")
shade = shade.replace(
    "base, normal, material, nullptr, &point_light, nullptr, 1.0F, world_position",
    "base, normal, material, specular_reflectance, nullptr, &point_light, nullptr, 1.0F, world_position")
shade = shade.replace(
    "                    material,\n                    &light.directional,",
    "                    material,\n                    specular_reflectance,\n                    &light.directional,")
shade = shade.replace(
    "                    material,\n                    nullptr,\n                    &light.point,",
    "                    material,\n                    specular_reflectance,\n                    nullptr,\n                    &light.point,")
shade = shade.replace(
    "                    material,\n                    nullptr,\n                    nullptr,\n                    &light.spot,",
    "                    material,\n                    specular_reflectance,\n                    nullptr,\n                    nullptr,\n                    &light.spot,")
shade = shade.replace(
    "normal, world_position, material, fixed_lights)",
    "normal, world_position, specular_reflectance, fixed_lights)")
if "light_contribution(\n                    base,\n                    normal,\n                    material,\n                    &" in shade:
    raise SystemExit("rasterizer.cpp: a fixed-light call still lacks specular reflectance")
text = text[:shade_start] + shade + text[shade_end:]
p.write_text(text)

# Fingerprints preserve every historical map_Ks-free value while incorporating the new role.
replace_once(
    "include/tiny_renderer/model_fingerprint.hpp",
    "    bool has_emissive_material = false;",
    "    bool has_specular_texture = false;\n"
    "    for (const MaterialDraw& draw : asset.draws) {\n"
    "        if (draw.specular_texture) {\n"
    "            has_specular_texture = true;\n"
    "            break;\n"
    "        }\n"
    "    }\n"
    "    if (has_specular_texture) {\n"
    "        detail::model_fingerprint_string(hash, \"material-specular-texture-v1\");\n"
    "        for (const MaterialDraw& draw : asset.draws) {\n"
    "            detail::model_fingerprint_texture(hash, draw.specular_texture);\n"
    "        }\n"
    "    }\n\n"
    "    bool has_emissive_material = false;")

# Extend the established specular test executable.
replace_once(
    "tests/test_specular_lighting.cpp",
    "#include <iostream>\n#include <limits>",
    "#include <iostream>\n#include <limits>\n#include <memory>\n#include <vector>")
replace_once(
    "tests/test_specular_lighting.cpp",
    "#include \"tiny_renderer/math.hpp\"",
    "#include \"tiny_renderer/math.hpp\"\n#include \"tiny_renderer/model_fingerprint.hpp\"")

tests_path = Path("tests/test_specular_lighting.cpp")
tests = tests_path.read_text()
insert_marker = "\n}  // namespace\n\nint main() {"
if tests.count(insert_marker) != 1:
    raise SystemExit("specular test namespace marker mismatch")
new_tests = r'''
VaryingPack uv_normal_varyings(float u, float v) {
    VaryingPack varyings;
    varyings.count = 5U;
    varyings.values[0] = u;
    varyings.values[1] = v;
    varyings.values[2] = 0.0F;
    varyings.values[3] = 0.0F;
    varyings.values[4] = 1.0F;
    return varyings;
}

ModelAsset specular_textured_model(std::shared_ptr<const Texture2D> texture) {
    ModelAsset asset;
    asset.mesh.vertices = {
        Vertex::with_varyings({-0.7F, -0.7F, 0.0F}, uv_normal_varyings(0.0F, 0.0F)),
        Vertex::with_varyings({0.7F, -0.7F, 0.0F}, uv_normal_varyings(1.0F, 0.0F)),
        Vertex::with_varyings({0.0F, 0.7F, 0.0F}, uv_normal_varyings(0.5F, 1.0F)),
    };
    asset.mesh.triangles = {{{0U, 1U, 2U}}};
    MaterialDraw draw;
    draw.range = {0U, 1U};
    draw.material_name = "specular-textured";
    draw.material.albedo = {0.0F, 0.0F, 0.0F};
    draw.material.specular = {1.0F, 1.0F, 1.0F};
    draw.material.shininess = 32.0F;
    draw.specular_texture = std::move(texture);
    asset.draws.push_back(std::move(draw));
    return asset;
}

ModelRenderOptions specular_texture_options() {
    ModelRenderOptions options;
    options.directional_light = specular_light({0.0F, 0.0F, 4.0F});
    options.directional_light.normal = {2U, 3U, 4U};
    return options;
}

void test_map_ks_import_and_shared_linear_cache() {
    const std::filesystem::path fixture =
        std::filesystem::path(TINY_RENDERER_SOURCE_DIR)
        / "tests" / "fixtures" / "specular_textured.obj";
    const ModelAsset imported = load_obj_model_asset_file(fixture);
    check(imported.draws.size() == 1U,
          "map_Ks fixture produces one material draw");
    if (imported.draws.empty()) {
        return;
    }
    const MaterialDraw& draw = imported.draws[0];
    check(draw.specular_texture != nullptr,
          "map_Ks fixture owns a decoded specular texture");
    check(draw.opacity_texture != nullptr,
          "fixture owns its sibling linear opacity texture");
    check(draw.specular_texture.get() == draw.opacity_texture.get(),
          "linear map_Ks and map_d references share the decoded texture cache");
    if (draw.specular_texture) {
        check(draw.specular_texture->source_transfer_function()
                  == TextureTransferFunction::Linear,
              "map_Ks enters the linear material texture domain");
    }

    std::istringstream duplicate(
        "newmtl x\nKd 1 1 1\nmap_Ks checker.ppm\nmap_Ks checker.ppm\n");
    bool duplicate_threw = false;
    try {
        (void)load_mtl_assets(duplicate);
    } catch (const MtlParseError&) {
        duplicate_threw = true;
    }
    check(duplicate_threw,
          "duplicate map_Ks is rejected deterministically");

    std::istringstream legacy(
        "newmtl x\nKd 1 1 1\nmap_Ks checker.ppm\n");
    bool legacy_threw = false;
    try {
        (void)load_mtl(legacy);
    } catch (const MtlParseError&) {
        legacy_threw = true;
    }
    check(legacy_threw,
          "legacy strict MTL loader continues to reject map_Ks directives");
}

void test_specular_texture_modulates_direct_and_environment_specular() {
    const auto specular_texture = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{0.25F, 0.5F, 1.0F}});
    const ModelAsset asset = specular_textured_model(specular_texture);

    Framebuffer direct(65U, 65U);
    draw_model_asset(
        direct,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        specular_texture_options());
    const Vec3 direct_center = direct.color_at(32U, 32U);
    check_near(direct_center.x, 0.25F,
               "map_Ks modulates direct specular red once");
    check_near(direct_center.y, 0.5F,
               "map_Ks modulates direct specular green once");
    check_near(direct_center.z, 1.0F,
               "map_Ks modulates direct specular blue once");

    Texture2D environment(
        1U, 1U, std::vector<Vec3>{{0.8F, 0.4F, 0.2F}});
    ModelRenderOptions reflection_options;
    EnvironmentReflectionLight reflection;
    reflection.normal = {2U, 3U, 4U};
    reflection.viewer_position = {0.0F, 0.0F, 4.0F};
    reflection.environment.texture = &environment;
    reflection_options.fixed_lights.environment_reflection = reflection;

    Framebuffer reflected(65U, 65U);
    draw_model_asset(
        reflected,
        asset,
        Mat4::identity(), Mat4::identity(), Mat4::identity(),
        reflection_options);
    const Vec3 reflected_center = reflected.color_at(32U, 32U);
    check_near(reflected_center.x, 0.2F,
               "map_Ks resolved reflectance feeds environment reflection red");
    check_near(reflected_center.y, 0.2F,
               "map_Ks resolved reflectance feeds environment reflection green");
    check_near(reflected_center.z, 0.2F,
               "map_Ks resolved reflectance feeds environment reflection blue");
}

void test_specular_texture_prepared_lifetime_sampler_and_list_preflight() {
    auto owner = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{0.4F, 0.6F, 0.8F}});
    std::weak_ptr<const Texture2D> retained = owner;
    ModelAsset source = specular_textured_model(owner);
    const std::uint64_t with_texture_fingerprint = model_asset_fnv1a64(source);
    ModelAsset without_texture = source;
    without_texture.draws[0].specular_texture.reset();
    check(model_asset_fnv1a64(without_texture) != with_texture_fingerprint,
          "model fingerprint includes map_Ks semantic content");

    PreparedModelSubmission prepared = prepare_model_asset(
        std::move(source), specular_texture_options());
    owner.reset();
    check(!retained.expired(),
          "prepared plan retains map_Ks ownership after source lifetime ends");

    Framebuffer prepared_framebuffer(65U, 65U);
    draw_prepared_model(
        prepared_framebuffer,
        prepared,
        Mat4::identity(), Mat4::identity(), Mat4::identity());
    const Vec3 center = prepared_framebuffer.color_at(32U, 32U);
    check_near(center.x, 0.4F,
               "prepared plan samples retained map_Ks red");
    check_near(center.y, 0.6F,
               "prepared plan samples retained map_Ks green");
    check_near(center.z, 0.8F,
               "prepared plan samples retained map_Ks blue");

    ModelRenderOptions invalid_sampler;
    invalid_sampler.sampler.max_anisotropy = 2U;
    bool sampler_threw = false;
    try {
        (void)prepare_model_asset(
            specular_textured_model(std::make_shared<const Texture2D>(
                1U, 1U, std::vector<Vec3>{{1.0F, 1.0F, 1.0F}})),
            invalid_sampler);
    } catch (const std::invalid_argument&) {
        sampler_threw = true;
    }
    check(sampler_threw,
          "specular-only texture participates in shared sampler validation");

    const auto invalid_texels = std::make_shared<const Texture2D>(
        1U, 1U, std::vector<Vec3>{{1.1F, 0.5F, 0.5F}});
    bool texel_threw = false;
    try {
        (void)prepare_model_asset(specular_textured_model(invalid_texels));
    } catch (const std::invalid_argument&) {
        texel_threw = true;
    }
    check(texel_threw,
          "prepared model rejects out-of-range map_Ks reflectance before execution");

    MaterialState visible_material;
    visible_material.albedo = {0.7F, 0.1F, 0.1F};
    const PreparedModelSubmission first = prepare_model_asset(
        model_from_triangle(visible_material));

    ModelRenderOptions invalid_uv;
    invalid_uv.u_channel = 99U;
    const PreparedModelSubmission later = prepare_model_asset(
        specular_textured_model(std::make_shared<const Texture2D>(
            1U, 1U, std::vector<Vec3>{{1.0F, 1.0F, 1.0F}})),
        invalid_uv);
    const PreparedModelListEntry entries[] = {
        {&first, Mat4::identity()},
        {&later, Mat4::identity()},
    };
    Framebuffer framebuffer(65U, 65U);
    framebuffer.clear({0.13F, 0.17F, 0.19F}, 0.8F, 9U);
    const auto before = framebuffer.rgb8();
    const float before_depth = framebuffer.depth_at(32U, 32U);
    const std::uint8_t before_stencil = framebuffer.stencil_at(32U, 32U);
    bool uv_threw = false;
    try {
        draw_prepared_model_list(
            framebuffer,
            entries,
            Mat4::identity(), Mat4::identity());
    } catch (const std::out_of_range&) {
        uv_threw = true;
    }
    check(uv_threw,
          "later prepared-list map_Ks invalid UV binding is rejected");
    check(framebuffer.rgb8() == before,
          "later map_Ks UV rejection occurs before earlier list color writes");
    check(framebuffer.depth_at(32U, 32U) == before_depth,
          "later map_Ks UV rejection occurs before earlier list depth writes");
    check(framebuffer.stencil_at(32U, 32U) == before_stencil,
          "later map_Ks UV rejection occurs before earlier list stencil writes");
}
'''
tests = tests.replace(insert_marker, "\n" + new_tests.strip() + insert_marker, 1)
call_anchor = "        test_prepared_list_preflights_later_bad_world_transform_before_writes();\n"
if tests.count(call_anchor) != 1:
    raise SystemExit("specular test call anchor mismatch")
tests = tests.replace(
    call_anchor,
    call_anchor
    + "        test_map_ks_import_and_shared_linear_cache();\n"
    + "        test_specular_texture_modulates_direct_and_environment_specular();\n"
    + "        test_specular_texture_prepared_lifetime_sampler_and_list_preflight();\n",
    1)
tests_path.write_text(tests)
