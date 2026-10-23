#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace margelo::nitro::nitropdf {

/**
 * Implemented once per platform (ios/SystemFonts.mm, android/src/main/cpp/SystemFonts.cpp).
 * Returns a single-face TrueType/OpenType font program for a CSS family name (including the
 * generic families `serif`, `sans-serif` and `monospace`), or an empty vector if unavailable.
 * The returned face may lack the requested style; callers synthesize bold/italic.
 */
std::vector<uint8_t> loadSystemFont(const std::string& family, bool bold, bool italic);

} // namespace margelo::nitro::nitropdf
