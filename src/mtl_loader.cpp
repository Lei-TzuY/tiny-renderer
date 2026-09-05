#include "tiny_renderer/mtl_loader.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

namespace tiny_renderer {

MtlParseError::MtlParseError(std::size_t line, const std::string& message)
    : std::runtime_error("MTL line " + std::to_string(line) + ": " + message), line_(line) {}

namespace {

struct PendingMaterial {
    std::string name;
    MaterialAssetDefinition asset{};
    bool has_kd{false};
    bool has_map_kd{false};
    bool has_d{false};
};

[[noreturn]] void fail(std::size_t line, const std::string& message) {
    throw MtlParseError(line, message);
}

float parse_unit_float(const std::string& token, std::size_t line, const char* field) {
    float value{};
    const char* const begin = token.data();
    const char* const end = begin + token.size();
    const auto [ptr, error] = std::from_chars(begin, end, value, std::chars_format::general);
    if (error != std::errc{} || ptr != end || !std::isfinite(value) || value < 0.0F || value > 1.0F) {
        fail(line, std::string(field) + " must be a finite value within [0, 1]");
    }
    return value;
}

void validate_sibling_texture_filename(const std::string& token, std::size_t line) {
    const std::filesystem::path path(token);
    if (token.empty() || path.is_absolute() || path.has_parent_path()
        || token.find('/') != std::string::npos || token.find('\\') != std::string::npos
        || token == "." || token == "..") {
        fail(line, "map_Kd must name exactly one sibling texture file");
    }
}

void finalize_pending(
    std::optional<PendingMaterial>& pending,
    MaterialAssetLibrary& library,
    std::size_t line_for_error) {
    if (!pending) {
        return;
    }
    if (!pending->has_kd) {
        fail(line_for_error, "material '" + pending->name + "' is missing required Kd");
    }
    library.emplace(pending->name, pending->asset);
    pending.reset();
}

MaterialAssetLibrary parse_material_assets(std::istream& input, bool allow_map_kd) {
    MaterialAssetLibrary library;
    std::optional<PendingMaterial> pending;

    std::string line_text;
    std::size_t line_number = 0U;
    while (std::getline(input, line_text)) {
        ++line_number;
        const std::size_t comment = line_text.find('#');
        if (comment != std::string::npos) {
            line_text.erase(comment);
        }

        std::istringstream line(line_text);
        std::string directive;
        if (!(line >> directive)) {
            continue;
        }

        if (directive == "newmtl") {
            std::string name;
            std::string extra;
            if (!(line >> name) || (line >> extra)) {
                fail(line_number, "newmtl must contain exactly one non-empty material name");
            }
            finalize_pending(pending, library, line_number);
            if (library.find(name) != library.end()) {
                fail(line_number, "duplicate material name '" + name + "'");
            }
            pending = PendingMaterial{name, MaterialAssetDefinition{}, false, false, false};
            continue;
        }

        if (directive == "Kd") {
            if (!pending) {
                fail(line_number, "Kd requires a preceding newmtl");
            }
            if (pending->has_kd) {
                fail(line_number, "material '" + pending->name + "' defines Kd more than once");
            }
            std::string r_token;
            std::string g_token;
            std::string b_token;
            std::string extra;
            if (!(line >> r_token >> g_token >> b_token) || (line >> extra)) {
                fail(line_number, "Kd must contain exactly three components");
            }
            pending->asset.material.albedo = {
                parse_unit_float(r_token, line_number, "Kd red"),
                parse_unit_float(g_token, line_number, "Kd green"),
                parse_unit_float(b_token, line_number, "Kd blue"),
            };
            pending->has_kd = true;
            continue;
        }

        if (directive == "d") {
            if (!pending) {
                fail(line_number, "d requires a preceding newmtl");
            }
            if (pending->has_d) {
                fail(line_number, "material '" + pending->name + "' defines d more than once");
            }
            std::string opacity_token;
            std::string extra;
            if (!(line >> opacity_token) || (line >> extra)) {
                fail(line_number, "d must contain exactly one opacity component");
            }
            pending->asset.material.opacity = parse_unit_float(opacity_token, line_number, "d opacity");
            pending->has_d = true;
            continue;
        }

        if (directive == "map_Kd" && allow_map_kd) {
            if (!pending) {
                fail(line_number, "map_Kd requires a preceding newmtl");
            }
            if (pending->has_map_kd) {
                fail(line_number, "material '" + pending->name + "' defines map_Kd more than once");
            }
            std::string filename;
            std::string extra;
            if (!(line >> filename) || (line >> extra)) {
                fail(line_number, "map_Kd must contain exactly one filename and no options");
            }
            validate_sibling_texture_filename(filename, line_number);
            pending->asset.diffuse_map_filename = filename;
            pending->has_map_kd = true;
            continue;
        }

        fail(line_number, "unsupported MTL directive '" + directive + "'");
    }

    if (input.bad()) {
        throw std::runtime_error("failed while reading MTL stream");
    }
    finalize_pending(pending, library, line_number + 1U);
    return library;
}

}  // namespace

MaterialLibrary load_mtl(std::istream& input) {
    const MaterialAssetLibrary assets = parse_material_assets(input, false);
    MaterialLibrary library;
    for (const auto& [name, asset] : assets) {
        library.emplace(name, asset.material);
    }
    return library;
}

MaterialLibrary load_mtl_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open MTL file: " + path.string());
    }
    return load_mtl(input);
}

MaterialAssetLibrary load_mtl_assets(std::istream& input) {
    return parse_material_assets(input, true);
}

MaterialAssetLibrary load_mtl_assets_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open MTL file: " + path.string());
    }
    return load_mtl_assets(input);
}

}  // namespace tiny_renderer
