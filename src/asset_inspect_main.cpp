#include <exception>
#include <filesystem>
#include <iostream>

#include "tiny_renderer/model_inspection.hpp"
#include "tiny_renderer/obj_loader.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: tiny_renderer_asset_inspect <model.obj>\n";
        return 2;
    }

    try {
        const tiny_renderer::ModelAsset asset =
            tiny_renderer::load_obj_model_asset_file(std::filesystem::path(argv[1]));
        std::cout << tiny_renderer::inspect_model_asset(asset);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "tiny_renderer_asset_inspect: " << error.what() << '\n';
        return 1;
    }
}
