#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace nitropdf {

struct PdfImage {
  int width = 0;
  int height = 0;
  /** 1 = gray, 3 = RGB, 4 = CMYK (JPEG only). */
  int components = 3;
  /** `data` holds the original JPEG stream (embedded as DCTDecode) instead of raw samples. */
  bool isJpeg = false;
  /** Adobe CMYK JPEGs store inverted samples. */
  bool invertedCmyk = false;
  std::vector<uint8_t> data;
  /** Optional 8-bit alpha samples (soft mask). */
  std::vector<uint8_t> alpha;
};

/** Supports JPEG (passthrough, no re-encode) and PNG. Returns nullptr for anything else. */
std::shared_ptr<const PdfImage> decodeImage(std::vector<uint8_t> bytes);

} // namespace nitropdf
