cmake_minimum_required(VERSION 3.20)
project(tiny_renderer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TINY_RENDERER_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(tiny_renderer
    src/framebuffer.cpp
    src/rasterizer.cpp
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
endif()
