#include "HybridNitroPdf.hpp"

#include "HybridPdfDocument.hpp"
#include "core/FontManager.hpp"
#include "core/HtmlRenderer.hpp"

#include <stdexcept>
#include <utility>

namespace margelo::nitro::nitropdf {

namespace {

RenderOptions toRenderOptions(const PdfOptions& options) {
  RenderOptions render;
  render.html = options.html;
  render.baseUrl = options.baseUrl.value_or("");

  double width = 595.28, height = 841.89;
  switch (options.pageSize.value_or(PageSize::A4)) {
    case PageSize::A4:
      break;
    case PageSize::LETTER:
      width = 612;
      height = 792;
      break;
    case PageSize::LEGAL:
      width = 612;
      height = 1008;
      break;
  }
  if (options.width.has_value() && options.height.has_value()) {
    width = *options.width;
    height = *options.height;
  }
  if (options.landscape.value_or(false) && width < height) std::swap(width, height);
  render.pageWidth = width;
  render.pageHeight = height;

  if (options.margins.has_value()) {
    render.marginTop = options.margins->top;
    render.marginRight = options.margins->right;
    render.marginBottom = options.margins->bottom;
    render.marginLeft = options.margins->left;
  }
  return render;
}

} // namespace

std::shared_ptr<Promise<std::shared_ptr<HybridPdfDocumentSpec>>> HybridNitroPdf::generate(const PdfOptions& options) {
  return Promise<std::shared_ptr<HybridPdfDocumentSpec>>::async(
      [render = toRenderOptions(options)]() -> std::shared_ptr<HybridPdfDocumentSpec> {
        auto result = renderHtmlToPdf(render);
        return std::make_shared<HybridPdfDocument>(std::move(result.pdf), result.pageCount);
      });
}

void HybridNitroPdf::registerFont(const std::string& family, const std::string& path, double weight, bool italic) {
  if (family.empty()) throw std::invalid_argument("registerFont: family must not be empty");
  if (!(weight >= 1 && weight <= 1000)) throw std::invalid_argument("registerFont: weight must be between 1 and 1000");
  FontManager::shared().registerFont(family, path, int(weight), italic);
}

} // namespace margelo::nitro::nitropdf
