#include "HtmlRenderer.hpp"

#include "FontManager.hpp"
#include "ImageDecoder.hpp"
#include "PdfWriter.hpp"
#include "Resources.hpp"
#include "Utf8.hpp"

#include "litehtml.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace margelo::nitro::nitropdf {

namespace {

constexpr double kPxToPt = 72.0 / 96.0;
constexpr float kKappa = 0.5523f;
constexpr double kMaxPageSizePt = 14400;

struct FontHandle {
  ResolvedFont resolved;
  size_t pdfFont = 0;
  bool registered = false;
  float size = 0;
  /** CSS px per font unit. */
  float scale = 0;
  /** Distance from baseline to the bottom of the line box, including half the line gap. */
  float descent = 0;
  int decoration = litehtml::text_decoration_line_none;
  litehtml::web_color decorationColor;
  float decorationThickness = 0;
};

struct DrawOp {
  enum class Kind : uint8_t { Paint, PushClip, PopClip };
  Kind kind;
  bool atomic;
  float top;
  float bottom;
  size_t offset;
  size_t length;
};

uint16_t glyphFor(const TrueTypeFont& font, uint32_t cp) {
  uint16_t glyph = font.glyphId(cp);
  if (glyph == 0 && (cp == 0xA0 || cp == '\t' || cp == '\n' || cp == '\r')) glyph = font.glyphId(' ');
  return glyph;
}

uint32_t toUpper(uint32_t cp) {
  if ((cp >= 'a' && cp <= 'z') || (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7)) return cp - 32;
  if (cp == 0xFF) return 0x178;
  return cp;
}

uint32_t toLower(uint32_t cp) {
  if ((cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7)) return cp + 32;
  return cp;
}

bool borderVisible(const litehtml::border& b) {
  return b.width > 0 && b.style != litehtml::border_style_none && b.style != litehtml::border_style_hidden &&
         b.color.alpha > 0;
}

class PdfContainer final : public litehtml::document_container {
public:
  PdfContainer(PdfWriter& writer, std::string baseUrl, float viewportWidth, float viewportHeight, float pageWidthPx,
               float pageHeightPx)
      : writer_(writer), baseUrl_(std::move(baseUrl)), viewportWidth_(viewportWidth),
        viewportHeight_(viewportHeight), pageWidthPx_(pageWidthPx), pageHeightPx_(pageHeightPx) {
    arena_.reserve(64 * 1024);
  }

  const std::vector<DrawOp>& ops() const { return ops_; }
  const std::string& arena() const { return arena_; }
  const litehtml::web_color& rootBackground() const { return rootBackground_; }

  litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document*,
                                 litehtml::font_metrics* fm) override {
    auto handle = std::make_unique<FontHandle>();
    handle->resolved = FontManager::shared().resolve(descr.family, descr.weight, descr.style != litehtml::font_style_normal);
    const TrueTypeFont& font = *handle->resolved.font;
    const auto& m = font.metrics();
    handle->size = descr.size;
    handle->scale = descr.size / float(m.unitsPerEm);
    float ascent = float(m.ascender) * handle->scale;
    float descent = float(-m.descender) * handle->scale;
    float gap = float(std::max(0, m.lineGap)) * handle->scale;
    handle->descent = descent + gap / 2;
    handle->decoration = descr.decoration_line;
    handle->decorationColor = descr.decoration_color;
    if (!descr.decoration_thickness.is_predefined() && descr.decoration_thickness.units() == litehtml::css_units_px) {
      handle->decorationThickness = descr.decoration_thickness.val();
    }
    if (fm) {
      fm->font_size = descr.size;
      fm->ascent = ascent + gap / 2;
      fm->descent = handle->descent;
      fm->height = ascent + descent + gap;
      fm->x_height = float(m.xHeight) * handle->scale;
      fm->ch_width = float(font.advanceWidth(font.glyphId('0'))) * handle->scale;
      fm->draw_spaces = descr.decoration_line != litehtml::text_decoration_line_none;
      fm->sub_shift = descr.size / 5;
      fm->super_shift = descr.size / 3;
    }
    fonts_.push_back(std::move(handle));
    return reinterpret_cast<litehtml::uint_ptr>(fonts_.back().get());
  }

  void delete_font(litehtml::uint_ptr) override {}

  litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override {
    auto* font = reinterpret_cast<FontHandle*>(hFont);
    if (!font || !text) return 0;
    const TrueTypeFont& tt = *font->resolved.font;
    uint32_t units = 0;
    while (*text) units += tt.advanceWidth(glyphFor(tt, decodeUtf8(text)));
    return float(units) * font->scale;
  }

  void draw_text(litehtml::uint_ptr, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color,
                 const litehtml::position& pos) override {
    auto* font = reinterpret_cast<FontHandle*>(hFont);
    if (!font || !text || !*text || color.alpha == 0) return;
    const TrueTypeFont& tt = *font->resolved.font;
    if (!font->registered) {
      font->pdfFont = writer_.fontIndex(font->resolved.font);
      font->registered = true;
    }

    size_t start = arena_.size();
    float baseline = pos.y + pos.height - font->descent;
    bool bold = font->resolved.syntheticBold;
    bool wrap = bold || color.alpha < 255;
    if (wrap) arena_ += "q ";
    alpha(color.alpha);
    fill(color);
    if (bold) {
      stroke(color);
      num(font->size * 0.035);
      arena_ += "w 2 Tr ";
    }
    arena_ += "BT /F";
    arena_ += std::to_string(font->pdfFont + 1);
    arena_ += ' ';
    num(font->size);
    arena_ += "Tf 1 0 ";
    num(font->resolved.syntheticItalic ? 0.2 : 0);
    arena_ += "-1 ";
    num(pos.x);
    num(baseline);
    arena_ += "Tm <";
    uint32_t units = 0;
    const char* s = text;
    while (*s) {
      uint32_t cp = decodeUtf8(s);
      uint16_t glyph = glyphFor(tt, cp);
      writer_.useGlyph(font->pdfFont, glyph, cp);
      appendHex4(arena_, glyph);
      units += tt.advanceWidth(glyph);
    }
    arena_ += "> Tj ET";
    if (wrap) arena_ += " Q";
    arena_ += '\n';

    if (font->decoration != litehtml::text_decoration_line_none) {
      const auto& m = tt.metrics();
      float width = float(units) * font->scale;
      litehtml::web_color lineColor = font->decorationColor.is_current_color ? color : font->decorationColor;
      float thickness = font->decorationThickness > 0
                            ? font->decorationThickness
                            : std::max(1.0f, float(m.underlineThickness) * font->scale);
      auto line = [&](float y) {
        arena_ += "q ";
        alpha(lineColor.alpha);
        fill(lineColor);
        rect(pos.x, y - thickness / 2, width, thickness);
        arena_ += "f Q\n";
      };
      if (font->decoration & litehtml::text_decoration_line_underline) line(baseline - float(m.underlinePosition) * font->scale);
      if (font->decoration & litehtml::text_decoration_line_overline) line(baseline - float(m.ascender) * font->scale);
      if (font->decoration & litehtml::text_decoration_line_line_through) line(baseline - float(m.strikeoutPosition) * font->scale);
    }
    commit(start, pos.y, pos.y + pos.height, true);
  }

  litehtml::pixel_t pt_to_px(float pt) const override { return pt * 96.0f / 72.0f; }
  litehtml::pixel_t get_default_font_size() const override { return 16; }
  // Matches the WebView/browser default so existing HTML renders the same.
  const char* get_default_font_name() const override { return "serif"; }

  void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker& marker) override {
    const litehtml::position& p = marker.pos;
    if (!marker.image.empty()) {
      auto img = image(resolveUrl(marker.image, marker.baseurl && *marker.baseurl ? marker.baseurl : baseUrl_));
      if (img) {
        size_t start = arena_.size();
        placeImage(writer_.imageName(img), p.x, p.y, p.width > 0 ? p.width : float(img->width),
                   p.height > 0 ? p.height : float(img->height));
        commit(start, p.y, p.bottom(), true);
      }
      return;
    }
    if (marker.color.alpha == 0 || p.width <= 0 || p.height <= 0) return;
    size_t start = arena_.size();
    arena_ += "q ";
    alpha(marker.color.alpha);
    switch (marker.marker_type) {
      case litehtml::list_style_type_circle:
        stroke(marker.color);
        arena_ += "1 w ";
        ellipse(p.x + 0.5f, p.y + 0.5f, p.width - 1, p.height - 1);
        arena_ += "S";
        break;
      case litehtml::list_style_type_disc:
        fill(marker.color);
        ellipse(p.x, p.y, p.width, p.height);
        arena_ += "f";
        break;
      case litehtml::list_style_type_square:
        fill(marker.color);
        rect(p.x, p.y, p.width, p.height);
        arena_ += "f";
        break;
      default:
        arena_.resize(start);
        return;
    }
    arena_ += " Q\n";
    commit(start, p.y, p.bottom(), true);
  }

  void load_image(const char* src, const char* baseurl, bool) override { image(resolve(src, baseurl)); }

  void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override {
    auto img = image(resolve(src, baseurl));
    sz.width = img ? float(img->width) : 0;
    sz.height = img ? float(img->height) : 0;
  }

  void draw_image(litehtml::uint_ptr, const litehtml::background_layer& layer, const std::string& url,
                  const std::string& base_url) override {
    auto img = image(resolveUrl(url, base_url.empty() ? baseUrl_ : base_url));
    const litehtml::position& o = layer.origin_box;
    const litehtml::position& c = layer.clip_box;
    if (!img || o.width <= 0 || o.height <= 0 || c.width <= 0 || c.height <= 0) return;

    std::string name = writer_.imageName(img);
    size_t start = arena_.size();
    arena_ += "q ";
    clipLayer(layer);
    bool repeatX = layer.repeat == litehtml::background_repeat_repeat || layer.repeat == litehtml::background_repeat_repeat_x;
    bool repeatY = layer.repeat == litehtml::background_repeat_repeat || layer.repeat == litehtml::background_repeat_repeat_y;
    float x0 = repeatX ? o.x - std::ceil((o.x - c.x) / o.width) * o.width : o.x;
    float y0 = repeatY ? o.y - std::ceil((o.y - c.y) / o.height) * o.height : o.y;
    float x1 = repeatX ? c.right() : o.x + 1;
    float y1 = repeatY ? c.bottom() : o.y + 1;
    int tiles = 0;
    for (float y = y0; y < y1 && tiles < 10000; y += o.height) {
      for (float x = x0; x < x1 && tiles < 10000; x += o.width, tiles++) placeImage(name, x, y, o.width, o.height);
    }
    arena_ += "Q\n";
    bool atomic = !repeatX && !repeatY;
    commit(start, atomic ? o.y : c.y, atomic ? o.bottom() : c.bottom(), atomic);
  }

  void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer& layer,
                       const litehtml::web_color& color) override {
    if (color.alpha == 0) return;
    if (layer.is_root) {
      rootBackground_ = color;
      return;
    }
    size_t start = arena_.size();
    arena_ += "q ";
    alpha(color.alpha);
    rect(layer.clip_box.x, layer.clip_box.y, layer.clip_box.width, layer.clip_box.height);
    arena_ += "W n ";
    fill(color);
    roundedRect(layer.border_box, layer.border_radius);
    arena_ += "f Q\n";
    commit(start, layer.clip_box.top(), layer.clip_box.bottom(), false);
  }

  void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer& layer,
                            const litehtml::background_layer::linear_gradient& gradient) override {
    std::string function = gradientFunction(gradient.color_points);
    if (function.empty()) return;
    std::string body = " /ShadingType 2 /ColorSpace /DeviceRGB /Coords [";
    for (float v : {gradient.start.x, gradient.start.y, gradient.end.x, gradient.end.y}) {
      appendNumber(body, v);
      body += ' ';
    }
    body += "] /Function " + function + " /Extend [true true]";
    paintShading(layer, writer_.shadingName(std::move(body)), nullptr);
  }

  void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer& layer,
                            const litehtml::background_layer::radial_gradient& gradient) override {
    std::string function = gradientFunction(gradient.color_points);
    float rx = gradient.radius.x, ry = gradient.radius.y;
    if (function.empty() || rx <= 0 || ry <= 0) return;
    std::string body = " /ShadingType 3 /ColorSpace /DeviceRGB /Coords [";
    for (float v : {gradient.position.x, gradient.position.y, 0.0f, gradient.position.x, gradient.position.y, rx}) {
      appendNumber(body, v);
      body += ' ';
    }
    body += "] /Function " + function + " /Extend [true true]";
    // Scale the circular shading vertically around its center to get the CSS ellipse.
    std::string transform = "1 0 0 ";
    appendNumber(transform, ry / rx);
    transform += " 0 ";
    appendNumber(transform, gradient.position.y * (1 - ry / rx));
    transform += " cm ";
    paintShading(layer, writer_.shadingName(std::move(body)), &transform);
  }

  void draw_conic_gradient(litehtml::uint_ptr hdc, const litehtml::background_layer& layer,
                           const litehtml::background_layer::conic_gradient& gradient) override {
    if (!gradient.color_points.empty()) draw_solid_fill(hdc, layer, gradient.color_points.front().color);
  }

  void draw_borders(litehtml::uint_ptr, const litehtml::borders& borders, const litehtml::position& p, bool) override {
    const litehtml::border* sides[4] = {&borders.top, &borders.right, &borders.bottom, &borders.left};
    bool any = false;
    for (auto* side : sides) any = any || borderVisible(*side);
    if (!any) return;

    const litehtml::border_radiuses& r = borders.radius;
    bool rounded = r.top_left_x > 0 || r.top_right_x > 0 || r.bottom_right_x > 0 || r.bottom_left_x > 0;
    const litehtml::border& t = borders.top;
    bool uniform = true;
    for (auto* side : sides) {
      uniform = uniform && borderVisible(*side) && side->width == t.width && side->color == t.color &&
                side->style == t.style && t.style != litehtml::border_style_dashed &&
                t.style != litehtml::border_style_dotted;
    }

    size_t start = arena_.size();
    if (rounded && uniform) {
      float w = t.width;
      litehtml::position inner(p.x + w / 2, p.y + w / 2, p.width - w, p.height - w);
      litehtml::border_radiuses ir = r;
      litehtml::margins inset;
      inset.left = inset.right = inset.top = inset.bottom = w / 2;
      ir -= inset;
      arena_ += "q ";
      alpha(t.color.alpha);
      stroke(t.color);
      num(w);
      arena_ += "w ";
      roundedRect(inner, ir);
      arena_ += "S Q\n";
    } else {
      float x = p.x, y = p.y, right = p.right(), bottom = p.bottom();
      float wt = borders.top.width, wr = borders.right.width, wb = borders.bottom.width, wl = borders.left.width;
      float quads[4][8] = {
          {x, y, right, y, right - wr, y + wt, x + wl, y + wt},
          {right, y, right, bottom, right - wr, bottom - wb, right - wr, y + wt},
          {x, bottom, right, bottom, right - wr, bottom - wb, x + wl, bottom - wb},
          {x, y, x, bottom, x + wl, bottom - wb, x + wl, y + wt},
      };
      float lines[4][4] = {
          {x, y + wt / 2, right, y + wt / 2},
          {right - wr / 2, y, right - wr / 2, bottom},
          {x, bottom - wb / 2, right, bottom - wb / 2},
          {x + wl / 2, y, x + wl / 2, bottom},
      };
      for (int i = 0; i < 4; i++) {
        const litehtml::border& b = *sides[i];
        if (!borderVisible(b)) continue;
        arena_ += "q ";
        alpha(b.color.alpha);
        if (b.style == litehtml::border_style_dashed || b.style == litehtml::border_style_dotted) {
          stroke(b.color);
          num(b.width);
          arena_ += "w ";
          if (b.style == litehtml::border_style_dotted) {
            arena_ += "1 J [0 ";
            num(b.width * 2);
          } else {
            arena_ += "[";
            num(b.width * 3);
            num(b.width * 3);
          }
          arena_ += "] 0 d ";
          num(lines[i][0]);
          num(lines[i][1]);
          arena_ += "m ";
          num(lines[i][2]);
          num(lines[i][3]);
          arena_ += "l S Q\n";
        } else {
          fill(b.color);
          num(quads[i][0]);
          num(quads[i][1]);
          arena_ += "m ";
          for (int k = 2; k < 8; k += 2) {
            num(quads[i][k]);
            num(quads[i][k + 1]);
            arena_ += "l ";
          }
          arena_ += "h f Q\n";
        }
      }
    }
    commit(start, p.top(), p.bottom(), false);
  }

  void set_caption(const char* caption) override {
    if (caption) writer_.setTitle(caption);
  }

  void set_base_url(const char* base_url) override {
    if (base_url && *base_url) baseUrl_ = resolveUrl(base_url, baseUrl_);
  }

  void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override {}
  void on_anchor_click(const char*, const litehtml::element::ptr&) override {}
  void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
  void set_cursor(const char*) override {}

  void transform_text(litehtml::string& text, litehtml::text_transform tt) override {
    if (tt == litehtml::text_transform_none) return;
    std::string out;
    out.reserve(text.size());
    const char* s = text.c_str();
    bool wordStart = true;
    while (*s) {
      uint32_t cp = decodeUtf8(s);
      if (tt == litehtml::text_transform_uppercase || (tt == litehtml::text_transform_capitalize && wordStart)) {
        cp = toUpper(cp);
      } else if (tt == litehtml::text_transform_lowercase) {
        cp = toLower(cp);
      }
      wordStart = cp == ' ' || cp == '\t' || cp == '\n' || cp == '-' || cp == 0xA0;
      appendUtf8(out, cp);
    }
    text = std::move(out);
  }

  // External stylesheets are not loaded; inline <style> blocks are supported.
  void import_css(litehtml::string&, const litehtml::string&, litehtml::string&) override {}

  void set_clip(const litehtml::position& pos, const litehtml::border_radiuses& radius) override {
    size_t start = arena_.size();
    arena_ += "q ";
    roundedRect(pos, radius);
    arena_ += "W n\n";
    commit(start, pos.top(), pos.bottom(), false, DrawOp::Kind::PushClip);
  }

  void del_clip() override {
    size_t start = arena_.size();
    arena_ += "Q\n";
    commit(start, 0, 0, false, DrawOp::Kind::PopClip);
  }

  void get_viewport(litehtml::position& viewport) const override {
    viewport = litehtml::position(0, 0, viewportWidth_, viewportHeight_);
  }

  litehtml::element::ptr create_element(const char*, const litehtml::string_map&,
                                        const std::shared_ptr<litehtml::document>&) override {
    return nullptr;
  }

  void get_media_features(litehtml::media_features& media) const override {
    media.type = litehtml::media_type_print;
    media.width = viewportWidth_;
    media.height = viewportHeight_;
    media.device_width = pageWidthPx_;
    media.device_height = pageHeightPx_;
    media.color = 8;
    media.monochrome = 0;
    media.color_index = 0;
    media.resolution = 96;
  }

  void get_language(litehtml::string& language, litehtml::string& culture) const override {
    language = "en";
    culture = "";
  }

private:
  void commit(size_t start, float top, float bottom, bool atomic, DrawOp::Kind kind = DrawOp::Kind::Paint) {
    ops_.push_back({kind, atomic, top, bottom, start, arena_.size() - start});
  }

  void num(double value) {
    appendNumber(arena_, value);
    arena_ += ' ';
  }

  void color(const litehtml::web_color& c, const char* op) {
    num(c.red / 255.0);
    num(c.green / 255.0);
    num(c.blue / 255.0);
    arena_ += op;
    arena_ += ' ';
  }
  void fill(const litehtml::web_color& c) { color(c, "rg"); }
  void stroke(const litehtml::web_color& c) { color(c, "RG"); }

  void alpha(uint8_t value) {
    if (value == 255) return;
    arena_ += '/';
    arena_ += writer_.alphaStateName(value);
    arena_ += " gs ";
  }

  void rect(float x, float y, float w, float h) {
    num(x);
    num(y);
    num(w);
    num(h);
    arena_ += "re ";
  }

  void curve(float x1, float y1, float x2, float y2, float x3, float y3) {
    for (float v : {x1, y1, x2, y2, x3, y3}) num(v);
    arena_ += "c ";
  }

  void ellipse(float x, float y, float w, float h) {
    float rx = w / 2, ry = h / 2, cx = x + rx, cy = y + ry;
    num(cx + rx);
    num(cy);
    arena_ += "m ";
    curve(cx + rx, cy + ry * kKappa, cx + rx * kKappa, cy + ry, cx, cy + ry);
    curve(cx - rx * kKappa, cy + ry, cx - rx, cy + ry * kKappa, cx - rx, cy);
    curve(cx - rx, cy - ry * kKappa, cx - rx * kKappa, cy - ry, cx, cy - ry);
    curve(cx + rx * kKappa, cy - ry, cx + rx, cy - ry * kKappa, cx + rx, cy);
    arena_ += "h ";
  }

  void roundedRect(const litehtml::position& p, litehtml::border_radiuses r) {
    if (r.top_left_x <= 0 && r.top_right_x <= 0 && r.bottom_right_x <= 0 && r.bottom_left_x <= 0) {
      rect(p.x, p.y, p.width, p.height);
      return;
    }
    r.fix_values(p.width, p.height);
    float x = p.x, y = p.y, right = p.right(), bottom = p.bottom();
    num(x + r.top_left_x);
    num(y);
    arena_ += "m ";
    num(right - r.top_right_x);
    num(y);
    arena_ += "l ";
    curve(right - r.top_right_x * (1 - kKappa), y, right, y + r.top_right_y * (1 - kKappa), right, y + r.top_right_y);
    num(right);
    num(bottom - r.bottom_right_y);
    arena_ += "l ";
    curve(right, bottom - r.bottom_right_y * (1 - kKappa), right - r.bottom_right_x * (1 - kKappa), bottom,
          right - r.bottom_right_x, bottom);
    num(x + r.bottom_left_x);
    num(bottom);
    arena_ += "l ";
    curve(x + r.bottom_left_x * (1 - kKappa), bottom, x, bottom - r.bottom_left_y * (1 - kKappa), x,
          bottom - r.bottom_left_y);
    num(x);
    num(y + r.top_left_y);
    arena_ += "l ";
    curve(x, y + r.top_left_y * (1 - kKappa), x + r.top_left_x * (1 - kKappa), y, x + r.top_left_x, y);
    arena_ += "h ";
  }

  void clipLayer(const litehtml::background_layer& layer) {
    roundedRect(layer.border_box, layer.border_radius);
    arena_ += "W n ";
    rect(layer.clip_box.x, layer.clip_box.y, layer.clip_box.width, layer.clip_box.height);
    arena_ += "W n ";
  }

  void placeImage(const std::string& name, float x, float y, float w, float h) {
    arena_ += "q ";
    num(w);
    arena_ += "0 0 ";
    num(-h);
    num(x);
    num(y + h);
    arena_ += "cm /";
    arena_ += name;
    arena_ += " Do Q\n";
  }

  void paintShading(const litehtml::background_layer& layer, const std::string& name, const std::string* transform) {
    if (layer.clip_box.width <= 0 || layer.clip_box.height <= 0) return;
    size_t start = arena_.size();
    arena_ += "q ";
    clipLayer(layer);
    if (transform) arena_ += *transform;
    arena_ += '/';
    arena_ += name;
    arena_ += " sh Q\n";
    commit(start, layer.clip_box.top(), layer.clip_box.bottom(), false);
  }

  static std::string gradientFunction(const std::vector<litehtml::background_layer::color_point>& input) {
    if (input.empty()) return "";
    std::vector<litehtml::background_layer::color_point> stops = input;
    for (size_t i = 0; i < stops.size(); i++) {
      stops[i].offset = std::clamp(stops[i].offset, 0.0f, 1.0f);
      if (i > 0) stops[i].offset = std::max(stops[i].offset, stops[i - 1].offset);
    }
    if (stops.front().offset > 0) stops.insert(stops.begin(), {0.0f, stops.front().color});
    if (stops.back().offset < 1) stops.push_back({1.0f, stops.back().color});
    if (stops.size() == 1) stops.push_back(stops.front());

    auto rgb = [](const litehtml::web_color& c) {
      std::string s = "[";
      for (int v : {c.red, c.green, c.blue}) {
        appendNumber(s, v / 255.0);
        s += ' ';
      }
      return s + "]";
    };
    auto segment = [&](const litehtml::background_layer::color_point& a, const litehtml::background_layer::color_point& b) {
      return "<< /FunctionType 2 /Domain [0 1] /C0 " + rgb(a.color) + " /C1 " + rgb(b.color) + " /N 1 >>";
    };
    if (stops.size() == 2) return segment(stops[0], stops[1]);

    std::string functions, bounds, encode;
    float previous = 0;
    for (size_t i = 0; i + 1 < stops.size(); i++) {
      functions += segment(stops[i], stops[i + 1]) + " ";
      encode += "0 1 ";
      if (i > 0) {
        // PDF requires strictly increasing bounds; hard color stops share an offset.
        float bound = std::min(1.0f, std::max(stops[i].offset, previous + 0.001f));
        appendNumber(bounds, bound);
        bounds += ' ';
        previous = bound;
      }
    }
    return "<< /FunctionType 3 /Domain [0 1] /Functions [" + functions + "] /Bounds [" + bounds + "] /Encode [" +
           encode + "] >>";
  }

  std::string resolve(const char* src, const char* baseurl) const {
    return resolveUrl(src ? src : "", baseurl && *baseurl ? baseurl : baseUrl_);
  }

  std::shared_ptr<const PdfImage> image(const std::string& url) {
    if (url.empty()) return nullptr;
    auto it = images_.find(url);
    if (it != images_.end()) return it->second;
    std::shared_ptr<const PdfImage> decoded;
    std::vector<uint8_t> bytes;
    if (loadResource(url, bytes)) decoded = decodeImage(std::move(bytes));
    images_[url] = decoded;
    return decoded;
  }

  PdfWriter& writer_;
  std::string baseUrl_;
  float viewportWidth_;
  float viewportHeight_;
  float pageWidthPx_;
  float pageHeightPx_;
  std::vector<std::unique_ptr<FontHandle>> fonts_;
  std::unordered_map<std::string, std::shared_ptr<const PdfImage>> images_;
  std::vector<DrawOp> ops_;
  std::string arena_;
  litehtml::web_color rootBackground_ = litehtml::web_color::transparent;
};

/** Page break positions (CSS px): moves breaks up so text lines and images aren't split when they fit on a page. */
std::vector<float> paginate(const std::vector<DrawOp>& ops, float docHeight, float pageHeight) {
  std::vector<std::pair<float, float>> atoms;
  for (const DrawOp& op : ops) {
    if (op.atomic && op.bottom > op.top && op.bottom - op.top <= pageHeight) atoms.emplace_back(op.top, op.bottom);
  }
  std::vector<float> breaks = {0};
  float start = 0;
  while (docHeight - start > pageHeight + 0.5f) {
    float cut = start + pageHeight;
    bool moved = true;
    while (moved) {
      moved = false;
      for (const auto& [top, bottom] : atoms) {
        if (top < cut && bottom > cut && top > start + 0.5f) {
          cut = top;
          moved = true;
        }
      }
    }
    breaks.push_back(cut);
    start = cut;
  }
  breaks.push_back(docHeight);
  return breaks;
}

} // namespace

RenderResult renderHtmlToPdf(const RenderOptions& options) {
  const double contentWidthPt = options.pageWidth - options.marginLeft - options.marginRight;
  const double contentHeightPt = options.pageHeight - options.marginTop - options.marginBottom;
  if (!(options.pageWidth > 0 && options.pageHeight > 0) || options.pageWidth > kMaxPageSizePt ||
      options.pageHeight > kMaxPageSizePt) {
    throw std::invalid_argument("Page size must be between 1 and 14400 points");
  }
  if (!(contentWidthPt >= 1 && contentHeightPt >= 1) || options.marginTop < 0 || options.marginRight < 0 ||
      options.marginBottom < 0 || options.marginLeft < 0) {
    throw std::invalid_argument("Margins must be non-negative and leave a printable area");
  }

  const float contentWidth = float(contentWidthPt / kPxToPt);
  const float contentHeight = float(contentHeightPt / kPxToPt);
  const float pageWidthPx = float(options.pageWidth / kPxToPt);
  const float marginLeftPx = float(options.marginLeft / kPxToPt);

  PdfWriter writer;
  PdfContainer container(writer, options.baseUrl, contentWidth, contentHeight, pageWidthPx,
                         float(options.pageHeight / kPxToPt));
  auto document = litehtml::document::createFromString(options.html, &container);
  if (!document) throw std::runtime_error("Failed to parse HTML");
  document->render(contentWidth);
  document->draw(0, 0, 0, nullptr);

  float docHeight = std::max(1.0f, float(document->height()));
  for (const DrawOp& op : container.ops()) {
    if (op.atomic) docHeight = std::max(docHeight, op.bottom);
  }
  std::vector<float> breaks = paginate(container.ops(), docHeight, contentHeight);

  const std::string& arena = container.arena();
  const litehtml::web_color& background = container.rootBackground();
  std::string content;
  for (size_t page = 0; page + 1 < breaks.size(); page++) {
    float top = breaks[page], bottom = breaks[page + 1];
    content.clear();
    if (background.alpha > 0) {
      content += "q ";
      for (int v : {background.red, background.green, background.blue}) {
        appendNumber(content, v / 255.0);
        content += ' ';
      }
      content += "rg 0 0 ";
      appendNumber(content, options.pageWidth);
      content += ' ';
      appendNumber(content, options.pageHeight);
      content += " re f Q\n";
    }
    // Map CSS px (y down) to PDF points (y up), then shift this page's slice to the top margin.
    content += "q ";
    for (double v : {kPxToPt, 0.0, 0.0, -kPxToPt, options.marginLeft, options.pageHeight - options.marginTop}) {
      appendNumber(content, v);
      content += ' ';
    }
    content += "cm 1 0 0 1 0 ";
    appendNumber(content, -top);
    content += " cm ";
    for (float v : {-marginLeftPx, top, pageWidthPx, bottom - top}) {
      appendNumber(content, v);
      content += ' ';
    }
    content += "re W n\n";

    int depth = 0;
    for (const DrawOp& op : container.ops()) {
      if (op.kind == DrawOp::Kind::PushClip) {
        depth++;
      } else if (op.kind == DrawOp::Kind::PopClip) {
        if (depth == 0) continue;
        depth--;
      } else {
        bool visible = op.bottom > op.top ? (op.bottom > top && op.top < bottom) : (op.top >= top && op.top < bottom);
        if (!visible) continue;
      }
      content.append(arena, op.offset, op.length);
    }
    while (depth-- > 0) content += "Q\n";
    content += "Q\n";
    writer.addPage(options.pageWidth, options.pageHeight, content);
  }

  RenderResult result;
  result.pageCount = writer.pageCount();
  result.pdf = writer.finish();
  return result;
}

} // namespace margelo::nitro::nitropdf
