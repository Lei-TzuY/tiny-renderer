from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))


def replace_count(path: str, old: str, new: str, expected: int) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{path}: expected {expected} matches, found {count}: {old[:100]!r}")
    p.write_text(text.replace(old, new))


replace_once(
    "include/tiny_renderer/mtl_loader.hpp",
    "    std::optional<std::string> emissive_map_filename;\n};",
    "    std::optional<std::string> emissive_map_filename;\n"
    "    // Optional linear data texture mapped deterministically into the\n"
    "    // renderer's bounded [1, 1000] shininess exponent domain.\n"
    "    std::optional<std::string> shininess_map_filename;\n};")

replace_once(
    "include/tiny_renderer/model.hpp",
    "    std::shared_ptr<const Texture2D> emissive_texture;\n};",
    "    std::shared_ptr<const Texture2D> emissive_texture;\n"
    "    // Optional linear data map resolved to one per-fragment shininess\n"
    "    // value shared by direct and environment specular consumers.\n"
    "    std::shared_ptr<const Texture2D> shininess_texture;\n};")

replace_once(
    "include/tiny_renderer/obj_loader.hpp",
    "    std::shared_ptr<const Texture2D> specular_texture;\n"
    "    std::shared_ptr<const Texture2D> emissive_texture;\n};",
    "    std::shared_ptr<const Texture2D> specular_texture;\n"
    "    std::shared_ptr<const Texture2D> emissive_texture;\n"
    "    std::shared_ptr<const Texture2D> shininess_texture;\n};")
replace_once(
    "include/tiny_renderer/obj_loader.hpp",
    "// color role is caller-selectable; opacity, normal, specular, and emissive maps\n"
    "// are treated as linear data textures. Linear preserves historical import\n",
    "// color role is caller-selectable; opacity, normal, specular, emissive, and\n"
    "// shininess maps are treated as linear data textures. Linear preserves historical import\n")

replace_once(
    "include/tiny_renderer/rasterizer.hpp",
    "    // Optional linear/HDR emissive-radiance multiplier. It reuses the\n"
    "    // same UV channels, sampler, mip chain, and raster gradients.\n"
    "    const Texture2D* emissive_texture{nullptr};\n};",
    "    // Optional linear/HDR emissive-radiance multiplier. It reuses the\n"
    "    // same UV channels, sampler, mip chain, and raster gradients.\n"
    "    const Texture2D* emissive_texture{nullptr};\n"
    "    // Optional linear data map. Arithmetic-mean RGB is mapped from [0,1]\n"
    "    // into the bounded [1,1000] shininess exponent domain per fragment.\n"
    "    const Texture2D* shininess_texture{nullptr};\n};")
replace_once(
    "include/tiny_renderer/rasterizer.hpp",
    "// glossy policy is resolved once per Rasterizer/material draw before the\n"
    "// existing M66 angular-footprint sampler is used.\n",
    "// glossy policy is resolved once per Rasterizer/material draw when shininess\n"
    "// is uniform, or per fragment when a shininess texture is bound. Both paths\n"
    "// delegate to the existing M66 angular-footprint sampler.\n")
replace_once(
    "include/tiny_renderer/rasterizer.hpp",
    "        if (fixed_lights_.environment_reflection\n"
    "            && fixed_lights_.environment_reflection->environment.mip_policy\n"
    "                == EnvironmentReflectionMipPolicy::MaterialShininess) {\n",
    "        if (fixed_lights_.environment_reflection\n"
    "            && fixed_lights_.environment_reflection->environment.mip_policy\n"
    "                == EnvironmentReflectionMipPolicy::MaterialShininess\n"
    "            && texture_binding_.shininess_texture == nullptr) {\n")

replace_once(
    "src/mtl_loader.cpp",
    "    bool has_map_ks{false};\n    bool has_map_ke{false};\n",
    "    bool has_map_ks{false};\n    bool has_map_ns{false};\n    bool has_map_ke{false};\n")
replace_once(
    "src/mtl_loader.cpp",
    "        if (directive == \"map_Ke\" && allow_maps) {\n",
    "        if (directive == \"map_Ns\" && allow_maps) {\n"
    "            if (!pending) {\n"
    "                fail(line_number, \"map_Ns requires a preceding newmtl\");\n"
    "            }\n"
    "            if (pending->has_map_ns) {\n"
    "                fail(line_number, \"material '\" + pending->name + \"' defines map_Ns more than once\");\n"
    "            }\n"
    "            std::string filename;\n"
    "            std::string extra;\n"
    "            if (!(line >> filename) || (line >> extra)) {\n"
    "                fail(line_number, \"map_Ns must contain exactly one filename and no options\");\n"
    "            }\n"
    "            validate_sibling_texture_filename(filename, line_number, \"map_Ns\");\n"
    "            pending->asset.shininess_map_filename = filename;\n"
    "            pending->has_map_ns = true;\n"
    "            continue;\n"
    "        }\n\n"
    "        if (directive == \"map_Ke\" && allow_maps) {\n")

replace_once(
    "src/material_asset_loader.cpp",
    "    std::shared_ptr<const Texture2D> specular_texture;\n"
    "    std::shared_ptr<const Texture2D> emissive_texture;\n};",
    "    std::shared_ptr<const Texture2D> specular_texture;\n"
    "    std::shared_ptr<const Texture2D> emissive_texture;\n"
    "    std::shared_ptr<const Texture2D> shininess_texture;\n};")
replace_once(
    "src/material_asset_loader.cpp",
    "        || definition.specular_map_filename.has_value()\n"
    "        || definition.emissive_map_filename.has_value();",
    "        || definition.specular_map_filename.has_value()\n"
    "        || definition.emissive_map_filename.has_value()\n"
    "        || definition.shininess_map_filename.has_value();")
replace_once(
    "src/material_asset_loader.cpp",
    "        loaded.emissive_texture = load_owned_texture(\n"
    "            resolved.library_directory,\n"
    "            resolved.definition.emissive_map_filename,\n"
    "            TextureTransferFunction::Linear,\n"
    "            texture_cache);\n"
    "        materials.emplace(name, std::move(loaded));",
    "        loaded.emissive_texture = load_owned_texture(\n"
    "            resolved.library_directory,\n"
    "            resolved.definition.emissive_map_filename,\n"
    "            TextureTransferFunction::Linear,\n"
    "            texture_cache);\n"
    "        loaded.shininess_texture = load_owned_texture(\n"
    "            resolved.library_directory,\n"
    "            resolved.definition.shininess_map_filename,\n"
    "            TextureTransferFunction::Linear,\n"
    "            texture_cache);\n"
    "        materials.emplace(name, std::move(loaded));")
replace_once(
    "src/material_asset_loader.cpp",
    "            draw.specular_texture = definition.specular_texture;\n"
    "            draw.emissive_texture = definition.emissive_texture;\n",
    "            draw.specular_texture = definition.specular_texture;\n"
    "            draw.emissive_texture = definition.emissive_texture;\n"
    "            draw.shininess_texture = definition.shininess_texture;\n")
replace_once(
    "src/material_asset_loader.cpp",
    "        batch.specular_texture = draw.specular_texture;\n"
    "        batch.emissive_texture = draw.emissive_texture;\n",
    "        batch.specular_texture = draw.specular_texture;\n"
    "        batch.emissive_texture = draw.emissive_texture;\n"
    "        batch.shininess_texture = draw.shininess_texture;\n")

replace_once(
    "src/model_renderer.cpp",
    "            || static_cast<bool>(draw.specular_texture)\n"
    "            || static_cast<bool>(draw.emissive_texture);",
    "            || static_cast<bool>(draw.specular_texture)\n"
    "            || static_cast<bool>(draw.emissive_texture)\n"
    "            || static_cast<bool>(draw.shininess_texture);")
replace_once(
    "src/model_renderer.cpp",
    "        if (draw.emissive_texture && !draw.emissive_texture->texels_nonnegative()) {\n"
    "            throw std::invalid_argument(\n"
    "                \"model emissive texture texels must be finite and non-negative\");\n"
    "        }\n",
    "        if (draw.emissive_texture && !draw.emissive_texture->texels_nonnegative()) {\n"
    "            throw std::invalid_argument(\n"
    "                \"model emissive texture texels must be finite and non-negative\");\n"
    "        }\n"
    "        if (draw.shininess_texture && !draw.shininess_texture->texels_within_unit_range()) {\n"
    "            throw std::invalid_argument(\n"
    "                \"model shininess texture texels must be finite and within [0, 1]\");\n"
    "        }\n")
replace_once(
    "src/model_renderer.cpp",
    "    binding.specular_texture = draw.specular_texture.get();\n"
    "    binding.emissive_texture = draw.emissive_texture.get();\n",
    "    binding.specular_texture = draw.specular_texture.get();\n"
    "    binding.emissive_texture = draw.emissive_texture.get();\n"
    "    binding.shininess_texture = draw.shininess_texture.get();\n")

replace_once(
    "src/rasterizer_validation.cpp",
    "        || texture_binding.specular_texture != nullptr\n"
    "        || texture_binding.emissive_texture != nullptr;",
    "        || texture_binding.specular_texture != nullptr\n"
    "        || texture_binding.emissive_texture != nullptr\n"
    "        || texture_binding.shininess_texture != nullptr;")
replace_once(
    "src/rasterizer_validation.cpp",
    "    if (texture_binding.emissive_texture != nullptr\n"
    "        && !texture_binding.emissive_texture->texels_nonnegative()) {\n"
    "        throw std::invalid_argument(\n"
    "            \"emissive texture texels must be finite and non-negative\");\n"
    "    }\n",
    "    if (texture_binding.emissive_texture != nullptr\n"
    "        && !texture_binding.emissive_texture->texels_nonnegative()) {\n"
    "        throw std::invalid_argument(\n"
    "            \"emissive texture texels must be finite and non-negative\");\n"
    "    }\n"
    "    if (texture_binding.shininess_texture != nullptr\n"
    "        && !texture_binding.shininess_texture->texels_within_unit_range()) {\n"
    "        throw std::invalid_argument(\n"
    "            \"shininess texture texels must be finite and within [0, 1]\");\n"
    "    }\n")

replace_once(
    "include/tiny_renderer/environment_lighting.hpp",
    "inline EnvironmentReflectionState resolve_material_reflection_state(\n"
    "    const EnvironmentReflectionState& state,\n"
    "    float material_shininess) {\n"
    "    validate_environment_reflection_state(state);\n"
    "    if (state.mip_policy != EnvironmentReflectionMipPolicy::MaterialShininess) {\n"
    "        return state;\n"
    "    }\n",
    "inline EnvironmentReflectionState resolve_material_reflection_state_unchecked(\n"
    "    const EnvironmentReflectionState& state,\n"
    "    float material_shininess) {\n"
    "    if (state.mip_policy != EnvironmentReflectionMipPolicy::MaterialShininess) {\n"
    "        return state;\n"
    "    }\n")
replace_once(
    "include/tiny_renderer/environment_lighting.hpp",
    "    resolved.angular_footprint_radians =\n"
    "        state.angular_footprint_radians / material_shininess;\n"
    "    return resolved;\n"
    "}\n\n"
    "}  // namespace environment_lighting_detail",
    "    resolved.angular_footprint_radians =\n"
    "        state.angular_footprint_radians / material_shininess;\n"
    "    return resolved;\n"
    "}\n\n"
    "inline EnvironmentReflectionState resolve_material_reflection_state(\n"
    "    const EnvironmentReflectionState& state,\n"
    "    float material_shininess) {\n"
    "    validate_environment_reflection_state(state);\n"
    "    return resolve_material_reflection_state_unchecked(state, material_shininess);\n"
    "}\n\n"
    "}  // namespace environment_lighting_detail")

replace_once(
    "src/rasterizer.cpp",
    "        && binding.specular_texture == nullptr\n"
    "        && binding.emissive_texture == nullptr) {",
    "        && binding.specular_texture == nullptr\n"
    "        && binding.emissive_texture == nullptr\n"
    "        && binding.shininess_texture == nullptr) {")
replace_once(
    "src/rasterizer.cpp",
    "    if (binding.emissive_texture != nullptr\n"
    "        && !binding.emissive_texture->texels_nonnegative()) {\n"
    "        throw std::invalid_argument(\n"
    "            \"emissive texture texels must be finite and non-negative\");\n"
    "    }\n",
    "    if (binding.emissive_texture != nullptr\n"
    "        && !binding.emissive_texture->texels_nonnegative()) {\n"
    "        throw std::invalid_argument(\n"
    "            \"emissive texture texels must be finite and non-negative\");\n"
    "    }\n"
    "    if (binding.shininess_texture != nullptr\n"
    "        && !binding.shininess_texture->texels_within_unit_range()) {\n"
    "        throw std::invalid_argument(\n"
    "            \"shininess texture texels must be finite and within [0, 1]\");\n"
    "    }\n")
replace_once(
    "src/rasterizer.cpp",
    "        || texture_binding.specular_texture != nullptr\n"
    "        || texture_binding.emissive_texture != nullptr) {",
    "        || texture_binding.specular_texture != nullptr\n"
    "        || texture_binding.emissive_texture != nullptr\n"
    "        || texture_binding.shininess_texture != nullptr) {")
replace_once(
    "src/rasterizer.cpp",
    "    if (source != BaseColorSource::Texture\n"
    "        && binding.opacity_texture == nullptr\n"
    "        && binding.normal_texture == nullptr\n"
    "        && binding.specular_texture == nullptr) {",
    "    if (source != BaseColorSource::Texture\n"
    "        && binding.opacity_texture == nullptr\n"
    "        && binding.normal_texture == nullptr\n"
    "        && binding.specular_texture == nullptr\n"
    "        && binding.emissive_texture == nullptr\n"
    "        && binding.shininess_texture == nullptr) {")
replace_once(
    "src/rasterizer.cpp",
    "            && binding.normal_texture == nullptr\n"
    "            && binding.specular_texture == nullptr\n"
    "        && binding.emissive_texture == nullptr)) {",
    "            && binding.normal_texture == nullptr\n"
    "            && binding.specular_texture == nullptr\n"
    "            && binding.emissive_texture == nullptr\n"
    "            && binding.shininess_texture == nullptr)) {")
replace_once(
    "src/rasterizer.cpp",
    "Vec3 fragment_emissive_radiance(\n",
    "float fragment_shininess(\n"
    "    const VaryingPack& varyings,\n"
    "    const TextureGradients& gradients,\n"
    "    const TextureBinding& texture_binding,\n"
    "    const MaterialState& material) {\n"
    "    if (texture_binding.shininess_texture == nullptr) {\n"
    "        return material.shininess;\n"
    "    }\n"
    "    const Vec3 sampled = texture_binding.shininess_texture->sample_grad(\n"
    "        {varyings.values[texture_binding.u_channel], varyings.values[texture_binding.v_channel]},\n"
    "        gradients,\n"
    "        texture_binding.sampler);\n"
    "    if (!finite_vec3(sampled)\n"
    "        || sampled.x < 0.0F || sampled.x > 1.0F\n"
    "        || sampled.y < 0.0F || sampled.y > 1.0F\n"
    "        || sampled.z < 0.0F || sampled.z > 1.0F) {\n"
    "        throw std::logic_error(\"validated shininess texture produced an invalid sample\");\n"
    "    }\n"
    "    // Bounded teaching rule: arithmetic-mean linear RGB maps [0,1]\n"
    "    // monotonically to the existing MTL Ns exponent domain [1,1000].\n"
    "    // This is a scalar data mapping, not luminance, roughness, or PBR.\n"
    "    const float scalar = (sampled.x + sampled.y + sampled.z) / 3.0F;\n"
    "    const float shininess = 1.0F + scalar * 999.0F;\n"
    "    if (!std::isfinite(shininess) || shininess < 1.0F || shininess > 1000.0F) {\n"
    "        throw std::logic_error(\"shininess texture mapping escaped the bounded exponent domain\");\n"
    "    }\n"
    "    return shininess;\n"
    "}\n\n"
    "Vec3 fragment_emissive_radiance(\n")
replace_once(
    "src/rasterizer.cpp",
    "Vec3 light_contribution(\n"
    "    const Vec3& base,\n"
    "    const Vec3& normal,\n"
    "    const MaterialState& material,\n"
    "    const Vec3& specular_reflectance,",
    "Vec3 light_contribution(\n"
    "    const Vec3& base,\n"
    "    const Vec3& normal,\n"
    "    float shininess,\n"
    "    const Vec3& specular_reflectance,")
replace_once(
    "src/rasterizer.cpp",
    "                    * std::pow(nh, material.shininess);",
    "                    * std::pow(nh, shininess);")
replace_once(
    "src/rasterizer.cpp",
    "Vec3 environment_reflection_contribution(\n"
    "    const Vec3& normal,\n"
    "    const Vec3& world_position,\n"
    "    const Vec3& specular_reflectance,\n"
    "    const FixedLightCollection& fixed_lights) {",
    "Vec3 environment_reflection_contribution(\n"
    "    const Vec3& normal,\n"
    "    const Vec3& world_position,\n"
    "    const Vec3& specular_reflectance,\n"
    "    float shininess,\n"
    "    const FixedLightCollection& fixed_lights) {")
replace_once(
    "src/rasterizer.cpp",
    "    const Vec3 reflected = incident - normal * (2.0F * dot(incident, normal));\n"
    "    const Vec3 radiance = environment_lighting_detail::reflection_environment_radiance_unchecked(\n"
    "        reflection.environment,\n"
    "        reflected);",
    "    const Vec3 reflected = incident - normal * (2.0F * dot(incident, normal));\n"
    "    EnvironmentReflectionState environment = reflection.environment;\n"
    "    if (environment.mip_policy == EnvironmentReflectionMipPolicy::MaterialShininess) {\n"
    "        environment = environment_lighting_detail::resolve_material_reflection_state_unchecked(\n"
    "            environment, shininess);\n"
    "    }\n"
    "    const Vec3 radiance = environment_lighting_detail::reflection_environment_radiance_unchecked(\n"
    "        environment,\n"
    "        reflected);")
replace_once(
    "src/rasterizer.cpp",
    "    // Resolve map_Ks exactly once; direct and environment specular share this value.\n"
    "    const Vec3 specular_reflectance = fragment_specular_reflectance(\n"
    "        varyings, gradients, texture_binding, material);\n",
    "    // Resolve map_Ks exactly once; direct and environment specular share this value.\n"
    "    const Vec3 specular_reflectance = fragment_specular_reflectance(\n"
    "        varyings, gradients, texture_binding, material);\n"
    "    // Resolve map_Ns exactly once. The same fragment-local exponent feeds\n"
    "    // every direct Blinn-Phong record and material-coupled environment LOD.\n"
    "    const float shininess = fragment_shininess(\n"
    "        varyings, gradients, texture_binding, material);\n")
replace_count(
    "src/rasterizer.cpp",
    "base, normal, material, specular_reflectance",
    "base, normal, shininess, specular_reflectance",
    2)
replace_count(
    "src/rasterizer.cpp",
    "                    material,\n                    specular_reflectance,",
    "                    shininess,\n                    specular_reflectance,",
    3)
replace_count(
    "src/rasterizer.cpp",
    "normal, world_position, specular_reflectance, fixed_lights",
    "normal, world_position, specular_reflectance, shininess, fixed_lights",
    2)

replace_once(
    "include/tiny_renderer/model_fingerprint.hpp",
    "    bool has_emissive_material = false;\n",
    "    bool has_shininess_texture = false;\n"
    "    for (const MaterialDraw& draw : asset.draws) {\n"
    "        if (draw.shininess_texture) {\n"
    "            has_shininess_texture = true;\n"
    "            break;\n"
    "        }\n"
    "    }\n"
    "    if (has_shininess_texture) {\n"
    "        detail::model_fingerprint_string(hash, \"material-shininess-texture-v1\");\n"
    "        for (const MaterialDraw& draw : asset.draws) {\n"
    "            detail::model_fingerprint_texture(hash, draw.shininess_texture);\n"
    "        }\n"
    "    }\n\n"
    "    bool has_emissive_material = false;\n")
replace_once(
    "src/model_inspection.cpp",
    "        append_texture_summary(output, (prefix + \"emissive_texture\").c_str(), draw.emissive_texture);\n",
    "        append_texture_summary(output, (prefix + \"emissive_texture\").c_str(), draw.emissive_texture);\n"
    "        append_texture_summary(output, (prefix + \"shininess_texture\").c_str(), draw.shininess_texture);\n")

replace_once(
    "CMakeLists.txt",
    "    add_test(NAME tiny_renderer_specular_lighting_tests COMMAND tiny_renderer_specular_lighting_tests)\n",
    "    add_test(NAME tiny_renderer_specular_lighting_tests COMMAND tiny_renderer_specular_lighting_tests)\n\n"
    "    add_executable(tiny_renderer_shininess_texture_tests tests/test_shininess_texture.cpp)\n"
    "    target_link_libraries(tiny_renderer_shininess_texture_tests PRIVATE tiny_renderer)\n"
    "    target_compile_definitions(tiny_renderer_shininess_texture_tests PRIVATE\n"
    "        TINY_RENDERER_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"\n"
    "    )\n"
    "    add_test(NAME tiny_renderer_shininess_texture_tests COMMAND tiny_renderer_shininess_texture_tests)\n")
