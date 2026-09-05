#include <exception>
#include <iostream>
#include <string>

#include "tiny_renderer/framebuffer.hpp"
#include "tiny_renderer/math.hpp"
#include "tiny_renderer/rasterizer.hpp"

using namespace tiny_renderer;

int main(int argc, char** argv) {
    try {
        const std::string output = argc > 1 ? argv[1] : "milestone1.ppm";
        Framebuffer framebuffer(320, 240);
        framebuffer.clear({0.035F, 0.045F, 0.07F});
        Rasterizer rasterizer(framebuffer);

        const Mat4 view = Mat4::look_at({0.0F, 0.0F, 0.5F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F});
        const Mat4 projection = Mat4::perspective(radians(60.0F), 320.0F / 240.0F, 0.1F, 10.0F);

        const Triangle far_triangle{{
            {{-1.15F, -0.75F, -3.1F}, {0.08F, 0.25F, 0.95F}},
            {{1.15F, -0.75F, -3.1F}, {0.25F, 0.85F, 1.00F}},
            {{0.0F, 1.05F, -3.1F}, {0.20F, 0.45F, 0.95F}},
        }};

        const Triangle near_triangle{{
            {{-0.8F, -0.45F, -2.1F}, {1.00F, 0.12F, 0.10F}},
            {{0.95F, -0.25F, -2.1F}, {1.00F, 0.75F, 0.05F}},
            {{0.05F, 0.95F, -2.1F}, {0.95F, 0.20F, 0.55F}},
        }};

        const Triangle clipped_triangle{{
            {{-2.6F, -0.1F, -2.7F}, {0.10F, 0.95F, 0.30F}},
            {{-0.65F, -0.95F, -2.7F}, {0.15F, 0.65F, 0.20F}},
            {{-0.55F, 0.35F, -2.7F}, {0.65F, 1.00F, 0.25F}},
        }};

        rasterizer.draw_triangle(far_triangle, Mat4::identity(), view, projection);
        rasterizer.draw_triangle(clipped_triangle, Mat4::rotation_y(radians(-7.0F)), view, projection);
        rasterizer.draw_triangle(near_triangle, Mat4::translation({0.12F, -0.02F, 0.0F}), view, projection);

        framebuffer.write_ppm(output);
        std::cout << "wrote " << output << " (320x240), framebuffer FNV-1a64=0x" << std::hex << framebuffer.fnv1a64() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tiny_renderer_sample: " << error.what() << '\n';
        return 1;
    }
}
