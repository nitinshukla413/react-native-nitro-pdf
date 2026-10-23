#include "HybridPdfDocument.hpp"

#include "core/Resources.hpp"

#include <cstdio>
#include <stdexcept>

namespace margelo::nitro::nitropdf {

HybridPdfDocument::HybridPdfDocument(std::vector<uint8_t> bytes, size_t pageCount)
    : HybridObject(TAG), _bytes(std::make_shared<std::vector<uint8_t>>(std::move(bytes))), _pageCount(pageCount) {}

std::shared_ptr<ArrayBuffer> HybridPdfDocument::toArrayBuffer() {
  // The buffer keeps the native bytes alive instead of copying them.
  auto bytes = _bytes;
  return ArrayBuffer::wrap(bytes->data(), bytes->size(), [bytes]() {});
}

std::shared_ptr<Promise<void>> HybridPdfDocument::saveToFile(const std::string& path) {
  auto bytes = _bytes;
  std::string target = ::nitropdf::pathFromFileUrl(path);
  return Promise<void>::async([bytes, target]() {
    if (target.empty()) throw std::invalid_argument("saveToFile: path must not be empty");
    FILE* file = std::fopen(target.c_str(), "wb");
    if (!file) throw std::runtime_error("saveToFile: cannot open " + target);
    size_t written = std::fwrite(bytes->data(), 1, bytes->size(), file);
    bool closed = std::fclose(file) == 0;
    if (written != bytes->size() || !closed) throw std::runtime_error("saveToFile: failed to write " + target);
  });
}

} // namespace margelo::nitro::nitropdf
