#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace margelo::nitro::nitropdf {

/** Converts a `file://` URL to a filesystem path (percent-decoded); other strings are returned as-is. */
std::string pathFromFileUrl(const std::string& urlOrPath);

/** Throws std::runtime_error if the file can't be read. */
std::vector<uint8_t> readFile(const std::string& path);

/** Resolves `src` against `baseUrl` (directory path or file:// URL). */
std::string resolveUrl(const std::string& src, const std::string& baseUrl);

/** Loads `data:` URLs and local files. Returns false for unsupported schemes or missing files. */
bool loadResource(const std::string& url, std::vector<uint8_t>& out);

} // namespace margelo::nitro::nitropdf
