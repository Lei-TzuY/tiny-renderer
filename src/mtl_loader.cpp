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
    MaterialState material{};
    bool has_kd{false};
    std::size_t line{};
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

void finalize_pending(
    std::optional<PendingMaterial>& pending,
    MaterialLibrary& library,
    std::size_t line_for_error) {
    if (!pending) {
        return;
    }
    if (!pending->has_kd) {
        fail(line_for_error, "material '" + pending->name + "' is missing required Kd");
    }
    library.emplace(pending->name, pending->material);
    pending.reset();
}

}  // namespace

MaterialLibrary load_mtl(std::istream& input) {
    MaterialLibrary library;
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
            pending = PendingMaterial{name, MaterialState{}, false, line_number};
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
            pending->material.albedo = {
                parse_unit_float(r_token, line_number, "Kd red"),
                parse_unit_float(g_token, line_number, "Kd green"),
                parse_unit_float(b_token, line_number, "Kd blue"),
            };
            pending->has_kd = true;
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

MaterialLibrary load_mtl_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open MTL file: " + path.string());
    }
    return load_mtl(input);
}

}  // namespace tiny_renderer
