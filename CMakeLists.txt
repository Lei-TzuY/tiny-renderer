cmake_minimum_required(VERSION 3.20)
project(tiny_renderer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TINY_RENDERER_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(tiny_renderer
    src/framebuffer.cpp
    src/framebuffer_output.cpp
    src/image_loader.cpp
    src/material_asset_loader.cpp
    src/model_inspection.cpp
    src/model_renderer.cpp
    src/offline_render.cpp
    src/mtl_loader.cpp
    src/obj_loader.cpp
    src/point_shadow.cpp
    src/ppm_loader.cpp
    src/rasterizer.cpp
    src/rasterizer_range.cpp
    src/rasterizer_validation.cpp
    src/shadow.cpp
    src/shadow_renderer.cpp
    src/spot_shadow.cpp
    src/texture.cpp
    src/tga_loader.cpp
)

target_include_directories(tiny_renderer PUBLIC include)

target_compile_options(tiny_renderer PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive->
)

if(TINY_RENDERER_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tiny_renderer PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(tiny_renderer PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

add_executable(tiny_renderer_sample src/main.cpp)
target_link_libraries(tiny_renderer_sample PRIVATE tiny_renderer)

add_executable(tiny_renderer_asset_inspect src/asset_inspect_main.cpp)
target_link_libraries(tiny_renderer_asset_inspect PRIVATE tiny_renderer)

add_executable(tiny_renderer_render src/render_main.cpp)
target_link_libraries(tiny_renderer_render PRIVATE tiny_renderer)

include(CTest)
if(BUILD_TESTING)
    add_executable(tiny_renderer_tests tests/test_main.cpp)
    target_link_libraries(tiny_renderer_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_tests COMMAND tiny_renderer_tests)

    add_executable(tiny_renderer_fixed_point_tests tests/test_fixed_point.cpp)
    target_link_libraries(tiny_renderer_fixed_point_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_fixed_point_tests COMMAND tiny_renderer_fixed_point_tests)

    add_executable(tiny_renderer_interpolation_tests tests/test_interpolation.cpp)
    target_link_libraries(tiny_renderer_interpolation_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_interpolation_tests COMMAND tiny_renderer_interpolation_tests)

    add_executable(tiny_renderer_texture_tests tests/test_texture.cpp)
    target_link_libraries(tiny_renderer_texture_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_texture_tests COMMAND tiny_renderer_texture_tests)

    add_executable(tiny_renderer_obj_tests tests/test_obj_loader.cpp)
    target_link_libraries(tiny_renderer_obj_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_obj_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_obj_tests COMMAND tiny_renderer_obj_tests)

    add_executable(tiny_renderer_obj_relative_index_tests tests/test_obj_relative_indices.cpp)
    target_link_libraries(tiny_renderer_obj_relative_index_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_obj_relative_index_tests COMMAND tiny_renderer_obj_relative_index_tests)

    add_executable(tiny_renderer_obj_model_source_tests tests/test_obj_model_source.cpp)
    target_link_libraries(tiny_renderer_obj_model_source_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_obj_model_source_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_obj_model_source_tests COMMAND tiny_renderer_obj_model_source_tests)

    add_executable(tiny_renderer_multi_material_library_tests
        tests/test_multiple_material_libraries.cpp)
    target_link_libraries(tiny_renderer_multi_material_library_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_multi_material_library_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(
        NAME tiny_renderer_multi_material_library_tests
        COMMAND tiny_renderer_multi_material_library_tests)

    add_executable(tiny_renderer_ppm_tests tests/test_ppm_loader.cpp)
    target_link_libraries(tiny_renderer_ppm_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_ppm_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_ppm_tests COMMAND tiny_renderer_ppm_tests)

    add_executable(tiny_renderer_tga_tests tests/test_tga_loader.cpp)
    target_link_libraries(tiny_renderer_tga_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_tga_tests COMMAND tiny_renderer_tga_tests)

    add_executable(tiny_renderer_lighting_tests tests/test_lighting.cpp)
    target_link_libraries(tiny_renderer_lighting_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_lighting_tests COMMAND tiny_renderer_lighting_tests)

    add_executable(tiny_renderer_material_tests tests/test_material.cpp)
    target_link_libraries(tiny_renderer_material_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_material_tests COMMAND tiny_renderer_material_tests)

    add_executable(tiny_renderer_material_import_tests tests/test_material_import.cpp)
    target_link_libraries(tiny_renderer_material_import_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_material_import_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_material_import_tests COMMAND tiny_renderer_material_import_tests)

    add_executable(tiny_renderer_model_asset_tests tests/test_model_asset.cpp)
    target_link_libraries(tiny_renderer_model_asset_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_model_asset_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_model_asset_tests COMMAND tiny_renderer_model_asset_tests)

    add_executable(tiny_renderer_asset_inspection_tests tests/test_asset_inspect.cpp)
    target_link_libraries(tiny_renderer_asset_inspection_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_asset_inspection_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_asset_inspection_tests COMMAND tiny_renderer_asset_inspection_tests)
    add_test(
        NAME tiny_renderer_asset_inspect_cli
        COMMAND tiny_renderer_asset_inspect
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
    )
    set_tests_properties(tiny_renderer_asset_inspect_cli PROPERTIES
        PASS_REGULAR_EXPRESSION "format=tiny-renderer-model-asset-inspect-v1"
    )
    add_test(NAME tiny_renderer_asset_inspect_usage COMMAND tiny_renderer_asset_inspect)
    set_tests_properties(tiny_renderer_asset_inspect_usage PROPERTIES WILL_FAIL TRUE)

    add_executable(tiny_renderer_direct_range_tests tests/test_direct_range.cpp)
    target_link_libraries(tiny_renderer_direct_range_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_direct_range_tests COMMAND tiny_renderer_direct_range_tests)

    add_executable(tiny_renderer_model_renderer_tests tests/test_model_renderer.cpp)
    target_link_libraries(tiny_renderer_model_renderer_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_model_renderer_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_model_renderer_tests COMMAND tiny_renderer_model_renderer_tests)

    add_executable(tiny_renderer_prepared_model_tests tests/test_prepared_model.cpp)
    target_link_libraries(tiny_renderer_prepared_model_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_prepared_model_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_prepared_model_tests COMMAND tiny_renderer_prepared_model_tests)

    add_executable(tiny_renderer_shared_preflight_tests tests/test_shared_preflight.cpp)
    target_link_libraries(tiny_renderer_shared_preflight_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_shared_preflight_tests COMMAND tiny_renderer_shared_preflight_tests)

    add_executable(tiny_renderer_model_instance_tests tests/test_model_instances.cpp)
    target_link_libraries(tiny_renderer_model_instance_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_model_instance_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_model_instance_tests COMMAND tiny_renderer_model_instance_tests)

    add_executable(tiny_renderer_prepared_model_list_tests tests/test_prepared_model_list.cpp)
    target_link_libraries(tiny_renderer_prepared_model_list_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_prepared_model_list_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_prepared_model_list_tests COMMAND tiny_renderer_prepared_model_list_tests)

    add_executable(tiny_renderer_culling_tests tests/test_culling.cpp)
    target_link_libraries(tiny_renderer_culling_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_culling_tests COMMAND tiny_renderer_culling_tests)

    add_executable(tiny_renderer_depth_state_tests tests/test_depth_state.cpp)
    target_link_libraries(tiny_renderer_depth_state_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_depth_state_tests COMMAND tiny_renderer_depth_state_tests)

    add_executable(tiny_renderer_viewport_scissor_tests tests/test_viewport_scissor.cpp)
    target_link_libraries(tiny_renderer_viewport_scissor_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_viewport_scissor_tests COMMAND tiny_renderer_viewport_scissor_tests)

    add_executable(tiny_renderer_stencil_tests tests/test_stencil.cpp)
    target_link_libraries(tiny_renderer_stencil_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_stencil_tests COMMAND tiny_renderer_stencil_tests)

    add_executable(tiny_renderer_stencil_integration_tests tests/test_stencil_integration.cpp)
    target_link_libraries(tiny_renderer_stencil_integration_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_stencil_integration_tests COMMAND tiny_renderer_stencil_integration_tests)

    add_executable(tiny_renderer_blend_tests tests/test_blend.cpp)
    target_link_libraries(tiny_renderer_blend_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_blend_tests COMMAND tiny_renderer_blend_tests)

    add_executable(tiny_renderer_blend_integration_tests tests/test_blend_integration.cpp)
    target_link_libraries(tiny_renderer_blend_integration_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_blend_integration_tests COMMAND tiny_renderer_blend_integration_tests)

    add_executable(tiny_renderer_opacity_tests tests/test_opacity.cpp)
    target_link_libraries(tiny_renderer_opacity_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_opacity_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_opacity_tests COMMAND tiny_renderer_opacity_tests)

    add_executable(tiny_renderer_msaa_tests tests/test_msaa.cpp)
    target_link_libraries(tiny_renderer_msaa_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_msaa_tests COMMAND tiny_renderer_msaa_tests)

    add_executable(tiny_renderer_output_transfer_tests tests/test_output_transfer.cpp)
    target_link_libraries(tiny_renderer_output_transfer_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_output_transfer_tests COMMAND tiny_renderer_output_transfer_tests)

    add_executable(tiny_renderer_pfm_header_boundary_tests tests/test_pfm_header_boundary.cpp)
    target_link_libraries(tiny_renderer_pfm_header_boundary_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_pfm_header_boundary_tests COMMAND tiny_renderer_pfm_header_boundary_tests)

    add_executable(tiny_renderer_display_mapping_tests tests/test_display_mapping.cpp)
    target_link_libraries(tiny_renderer_display_mapping_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_display_mapping_tests COMMAND tiny_renderer_display_mapping_tests)

    add_executable(tiny_renderer_offline_render_tests tests/test_offline_render.cpp)
    target_link_libraries(tiny_renderer_offline_render_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_offline_render_tests COMMAND tiny_renderer_offline_render_tests)

    add_executable(tiny_renderer_headless_environment_tests tests/test_headless_environment.cpp)
    target_link_libraries(tiny_renderer_headless_environment_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_headless_environment_tests COMMAND tiny_renderer_headless_environment_tests)

    add_test(
        NAME tiny_renderer_render_cli_ppm
        COMMAND tiny_renderer_render
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_cli.ppm"
            64 48 1
    )
    set_tests_properties(tiny_renderer_render_cli_ppm PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered format=ppm width=64 height=48 samples=1"
    )
    add_test(
        NAME tiny_renderer_render_cli_pfm
        COMMAND tiny_renderer_render
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_cli.pfm"
            48 64 4
    )
    set_tests_properties(tiny_renderer_render_cli_pfm PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered format=pfm width=48 height=64 samples=4"
    )

    add_test(
        NAME tiny_renderer_render_cli_environment_ppm_a
        COMMAND tiny_renderer_render
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_environment_a.ppm"
            40 30 1
            --environment "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/environment_hdr.pfm"
            --environment-intensity 1.25
            --environment-yaw 0.3
    )
    set_tests_properties(tiny_renderer_render_cli_environment_ppm_a PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered format=ppm width=40 height=30 samples=1"
    )
    add_test(
        NAME tiny_renderer_render_cli_environment_ppm_b
        COMMAND tiny_renderer_render
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_environment_b.ppm"
            40 30 1
            --environment "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/environment_hdr.pfm"
            --environment-intensity 1.25
            --environment-yaw 0.3
    )
    add_test(
        NAME tiny_renderer_render_cli_environment_ppm_compare
        COMMAND ${CMAKE_COMMAND} -E compare_files
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_environment_a.ppm"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_environment_b.ppm"
    )
    set_tests_properties(tiny_renderer_render_cli_environment_ppm_compare PROPERTIES
        DEPENDS "tiny_renderer_render_cli_environment_ppm_a;tiny_renderer_render_cli_environment_ppm_b"
    )

    add_test(
        NAME tiny_renderer_render_cli_environment_pfm_a
        COMMAND tiny_renderer_render
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_environment_a.pfm"
            40 30 4
            --environment "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/environment_hdr.pfm"
            --environment-intensity 1.25
            --environment-yaw 0.3
    )
    set_tests_properties(tiny_renderer_render_cli_environment_pfm_a PROPERTIES
        PASS_REGULAR_EXPRESSION "rendered format=pfm width=40 height=30 samples=4"
    )
    add_test(
        NAME tiny_renderer_render_cli_environment_pfm_b
        COMMAND tiny_renderer_render
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_environment_b.pfm"
            40 30 4
            --environment "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/environment_hdr.pfm"
            --environment-intensity 1.25
            --environment-yaw 0.3
    )
    add_test(
        NAME tiny_renderer_render_cli_environment_pfm_compare
        COMMAND ${CMAKE_COMMAND} -E compare_files
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_environment_a.pfm"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_environment_b.pfm"
    )
    set_tests_properties(tiny_renderer_render_cli_environment_pfm_compare PROPERTIES
        DEPENDS "tiny_renderer_render_cli_environment_pfm_a;tiny_renderer_render_cli_environment_pfm_b"
    )

    add_test(
        NAME tiny_renderer_render_cli_environment_duplicate
        COMMAND tiny_renderer_render
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_duplicate.ppm"
            --environment "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/environment_hdr.pfm"
            --environment "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/environment_hdr.pfm"
    )
    set_tests_properties(tiny_renderer_render_cli_environment_duplicate PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME tiny_renderer_render_cli_environment_orphan_intensity
        COMMAND tiny_renderer_render
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_orphan.ppm"
            --environment-intensity 2.0
    )
    set_tests_properties(tiny_renderer_render_cli_environment_orphan_intensity PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME tiny_renderer_render_cli_environment_missing_file
        COMMAND tiny_renderer_render
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/material_texture_sequence.obj"
            "${CMAKE_CURRENT_BINARY_DIR}/tiny_renderer_render_missing_environment.ppm"
            --environment "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/does_not_exist.pfm"
    )
    set_tests_properties(tiny_renderer_render_cli_environment_missing_file PROPERTIES WILL_FAIL TRUE)

    add_test(NAME tiny_renderer_render_usage COMMAND tiny_renderer_render)
    set_tests_properties(tiny_renderer_render_usage PROPERTIES WILL_FAIL TRUE)

    add_executable(tiny_renderer_opacity_texture_tests tests/test_opacity_texture.cpp)
    target_link_libraries(tiny_renderer_opacity_texture_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_opacity_texture_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_opacity_texture_tests COMMAND tiny_renderer_opacity_texture_tests)

    add_executable(tiny_renderer_alpha_to_coverage_tests tests/test_alpha_to_coverage.cpp)
    target_link_libraries(tiny_renderer_alpha_to_coverage_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_alpha_to_coverage_tests COMMAND tiny_renderer_alpha_to_coverage_tests)

    add_executable(tiny_renderer_shadow_tests tests/test_shadow.cpp)
    target_link_libraries(tiny_renderer_shadow_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_shadow_tests COMMAND tiny_renderer_shadow_tests)

    add_executable(tiny_renderer_alpha_test_tests tests/test_alpha_test.cpp)
    target_link_libraries(tiny_renderer_alpha_test_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_alpha_test_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_alpha_test_tests COMMAND tiny_renderer_alpha_test_tests)

    add_executable(tiny_renderer_fragment_program_tests tests/test_fragment_program.cpp)
    target_link_libraries(tiny_renderer_fragment_program_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_fragment_program_tests COMMAND tiny_renderer_fragment_program_tests)

    add_executable(tiny_renderer_vertex_program_tests tests/test_vertex_program.cpp)
    target_link_libraries(tiny_renderer_vertex_program_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_vertex_program_tests COMMAND tiny_renderer_vertex_program_tests)

    add_executable(tiny_renderer_normal_mapping_tests tests/test_normal_mapping.cpp)
    target_link_libraries(tiny_renderer_normal_mapping_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_normal_mapping_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_normal_mapping_tests COMMAND tiny_renderer_normal_mapping_tests)

    add_executable(tiny_renderer_specular_lighting_tests tests/test_specular_lighting.cpp)
    target_link_libraries(tiny_renderer_specular_lighting_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_specular_lighting_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_specular_lighting_tests COMMAND tiny_renderer_specular_lighting_tests)

    add_executable(tiny_renderer_point_light_tests tests/test_point_light.cpp)
    target_link_libraries(tiny_renderer_point_light_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_point_light_tests COMMAND tiny_renderer_point_light_tests)

    add_executable(tiny_renderer_multi_light_tests tests/test_multi_light.cpp)
    target_link_libraries(tiny_renderer_multi_light_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_multi_light_tests COMMAND tiny_renderer_multi_light_tests)

    add_executable(tiny_renderer_point_shadow_tests tests/test_point_shadow.cpp)
    target_link_libraries(tiny_renderer_point_shadow_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_point_shadow_tests COMMAND tiny_renderer_point_shadow_tests)

    add_executable(tiny_renderer_spotlight_tests tests/test_spotlight.cpp)
    target_link_libraries(tiny_renderer_spotlight_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_spotlight_tests COMMAND tiny_renderer_spotlight_tests)

    add_executable(tiny_renderer_spot_shadow_tests tests/test_spot_shadow.cpp)
    target_link_libraries(tiny_renderer_spot_shadow_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_spot_shadow_tests COMMAND tiny_renderer_spot_shadow_tests)
    add_executable(tiny_renderer_light_color_tests tests/test_light_color.cpp)
    target_link_libraries(tiny_renderer_light_color_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_light_color_tests COMMAND tiny_renderer_light_color_tests)

    add_executable(tiny_renderer_per_record_shadow_tests tests/test_per_record_shadows.cpp)
    target_link_libraries(tiny_renderer_per_record_shadow_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_per_record_shadow_tests COMMAND tiny_renderer_per_record_shadow_tests)

    add_executable(tiny_renderer_shadow_sampling_tests tests/test_shadow_sampling.cpp)
    target_link_libraries(tiny_renderer_shadow_sampling_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_shadow_sampling_tests COMMAND tiny_renderer_shadow_sampling_tests)

    add_executable(tiny_renderer_cascaded_shadow_tests tests/test_cascaded_shadow.cpp)
    target_link_libraries(tiny_renderer_cascaded_shadow_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_cascaded_shadow_tests COMMAND tiny_renderer_cascaded_shadow_tests)

endif()