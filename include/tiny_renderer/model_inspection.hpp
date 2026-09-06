#pragma once

#include <string>

#include "tiny_renderer/model.hpp"

namespace tiny_renderer {

// Returns a deterministic, line-oriented structural summary of one canonical
// ModelAsset. The format is versioned for tooling use and deliberately avoids
// pointer identity and implementation-dependent floating-point text formatting.
[[nodiscard]] std::string inspect_model_asset(const ModelAsset& asset);

}  // namespace tiny_renderer
