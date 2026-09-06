cmake_minimum_required(VERSION 3.20)
project(tiny_renderer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TINY_RENDERER_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(tiny_renderer
    src/framebuffer.cpp
    src/material_asset_loader.cpp
    src/model_renderer.cpp
    src/mtl_loader.cpp
    src/obj_loader.cpp
    src/ppm_loader.cpp
    src/rasterizer.cpp
    src/rasterizer_range.cpp
    src/rasterizer_validation.cpp
    src/texture.cpp
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

    add_executable(tiny_renderer_obj_model_source_tests tests/test_obj_model_source.cpp)
    target_link_libraries(tiny_renderer_obj_model_source_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_obj_model_source_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_obj_model_source_tests COMMAND tiny_renderer_obj_model_source_tests)

    add_executable(tiny_renderer_ppm_tests tests/test_ppm_loader.cpp)
    target_link_libraries(tiny_renderer_ppm_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_ppm_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_ppm_tests COMMAND tiny_renderer_ppm_tests)

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

    add_executable(tiny_renderer_opacity_texture_tests tests/test_opacity_texture.cpp)
    target_link_libraries(tiny_renderer_opacity_texture_tests PRIVATE tiny_renderer)
    target_compile_definitions(tiny_renderer_opacity_texture_tests PRIVATE
        TINY_RENDERER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME tiny_renderer_opacity_texture_tests COMMAND tiny_renderer_opacity_texture_tests)

    add_executable(tiny_renderer_alpha_to_coverage_tests tests/test_alpha_to_coverage.cpp)
    target_link_libraries(tiny_renderer_alpha_to_coverage_tests PRIVATE tiny_renderer)
    add_test(NAME tiny_renderer_alpha_to_coverage_tests COMMAND tiny_renderer_alpha_to_coverage_tests)
endif()
