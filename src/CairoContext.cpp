#include <string>
#include <map>
#include <algorithm>
#include <Rcpp.h>
#include <cairo.h>
#include <cairo-pdf.h>
#include <cairo-ft.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "gdtools_types.h"

using namespace Rcpp;

struct CairoContext::CairoContext_ {
  cairo_surface_t* surface;
  cairo_t* context;

  FT_Library library;
  fontCache fonts;
};

CairoContext::CairoContext() {
  cairo_ = new CairoContext_();
  cairo_->surface = cairo_pdf_surface_create(NULL, 720, 720);
  cairo_->context = cairo_create(cairo_->surface);

  if (!FcInit())
    Rcpp::stop("Fontconfig error: unable to initialize");
  if (FT_Init_FreeType(&(cairo_->library)))
    Rcpp::stop("FreeType error: unable to initialize FreeType library object");

  Rprintf("Cairo version: %s\n", cairo_version_string());
  int major, minor, patch = 0;
  FT_Library_Version(cairo_->library, &major, &minor, &patch);
  Rprintf("FT version: %d.%d.%d\n", major, minor, patch);
}

CairoContext::~CairoContext() {
  FcFini();

  fontCache::iterator it = cairo_->fonts.begin();
  while (it != cairo_->fonts.end()) {
    cairo_font_face_destroy(it->second);
    ++it;
  }

  cairo_surface_destroy(cairo_->surface);
  cairo_destroy(cairo_->context);

  delete cairo_;
}

void CairoContext::cacheFont(fontCache& cache, std::string& key,
                             std::string fontfile, int fontindex)  {
  FT_Face face;
  if (0 != FT_New_Face(cairo_->library, fontfile.c_str(), fontindex, &face))
    Rcpp::stop("FreeType error: unable to open %s", fontfile.c_str());

  cairo_font_face_t* cairo_face = cairo_ft_font_face_create_for_ft_face(face, 0);

  cairo_user_data_key_t font_key;
  cairo_status_t status = cairo_font_face_set_user_data(
    cairo_face, &font_key, face, (cairo_destroy_func_t) FT_Done_Face
  );
  if (status) {
    cairo_font_face_destroy(cairo_face);
    FT_Done_Face(face);
    Rcpp::stop("Cairo error: unable to handle %s", fontfile.c_str());
  }

  cache[key] = cairo_face;
}

// Defined in sys_fonts.cpp
FcPattern* fcFindMatch(const char* fontname, int bold, int italic);
std::string fcFindFontFile(FcPattern* match);
int fcFindFontIndex(const char* fontfile, int bold, int italic);

struct font_file_t {
  std::string file;
  int index;
};

font_file_t findFontFile(const char* fontname, int bold, int italic) {
  FcPattern* match = fcFindMatch(fontname, bold, italic);

  font_file_t output;
  FcChar8 *matched_file;
  if (match && FcPatternGetString(match, FC_FILE, 0, &matched_file) == FcResultMatch) {
    output.file = (const char*) matched_file;
    FcPatternGetInteger(match, FC_INDEX, 0, &(output.index));
  }
  FcPatternDestroy(match);

  if (output.file.size())
    return output;
  else
    Rcpp::stop("Fontconfig error: unable to match font pattern");
}

void CairoContext::setFont(std::string fontname, double fontsize,
                           bool bold, bool italic, std::string fontfile) {
  std::string key;
  if (fontfile.size()) {
    // Use file path as key to cached elements
    key = fontfile;
    if (cairo_->fonts.find(key) == cairo_->fonts.end()) {
      int index = fcFindFontIndex(fontfile.c_str(), bold, italic);
      cacheFont(cairo_->fonts, key, fontfile, index);
    }
  } else {
    // Use font name and bold/italic properties as key
    char props[20];
    snprintf(props, sizeof(props), " %d %d", (int) bold, (int) italic);
    key = fontname + props;
    if (cairo_->fonts.find(key) == cairo_->fonts.end()) {
      // Add font to cache
      font_file_t fontfile = findFontFile(fontname.c_str(), bold, italic);
      cacheFont(cairo_->fonts, key, fontfile.file, fontfile.index);
    }
  }

  cairo_set_font_size(cairo_->context, fontsize);
  cairo_set_font_face(cairo_->context, cairo_->fonts[key]);
}

FontMetric CairoContext::getExtents(std::string x) {
  cairo_text_extents_t te;
  cairo_text_extents(cairo_->context, x.c_str(), &te);

  FontMetric fm;
  fm.height = te.height;
  fm.width = te.x_advance;
  fm.ascent = -te.y_bearing;
  fm.descent = te.height + te.y_bearing;

  return fm;
}
