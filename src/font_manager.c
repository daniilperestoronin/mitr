/**
 * font_manager.c - Font management implementation
 *
 * All rasterised glyphs live in a single GPU texture atlas.  The hash
 * table maps (codepoint, effective-fg, bold, italic) to a rectangle
 * inside the atlas.  Because every SDL_RenderCopy in the renderer draws
 * from the same texture, SDL2 can batch them into one or two draw calls.
 */

#include "font_manager.h"
#include "font_resolver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLYPH_CACHE_SIZE 4096
#define GLYPH_CACHE_MAX_LOAD 3072
#define GLYPH_ATLAS_SIZE 1024
#define SYSTEM_FONT_CACHE_SIZE 16
#define FALLBACK_MISS_CACHE_SIZE 256

typedef struct {
  uint32_t codepoint;
  uint32_t fg_color;
  bool bold;
  bool italic;
  uint8_t face;
  bool occupied;
  SDL_Rect src_rect;
  int width;
  int height;
} GlyphCacheEntry;

typedef enum {
  GLYPH_FACE_PRIMARY = 0,
  GLYPH_FACE_FALLBACK = 1,
  GLYPH_FACE_SYMBOLS = 2,
  GLYPH_FACE_SYSTEM_BASE = 3
} GlyphFace;

typedef struct {
  FontMatch match;
  TTF_Font *font;
  TTF_Font *font_bold;
} SystemFontFace;

typedef struct {
  uint32_t codepoint;
  bool occupied;
} FallbackMiss;

struct FontManager {
  TTF_Font *font;
  TTF_Font *font_bold;
  TTF_Font *fallback;
  TTF_Font *fallback_bold;
  TTF_Font *symbols;
  TTF_Font *symbols_bold;
  bool fallback_explicit;
  FontResolver *resolver;
  SystemFontFace system_fonts[SYSTEM_FONT_CACHE_SIZE];
  size_t system_font_count;
  FallbackMiss fallback_misses[FALLBACK_MISS_CACHE_SIZE];
  int char_width;
  int char_height;
  int font_size;

  SDL_Renderer *sdl_renderer;

  SDL_Texture *atlas;
  int atlas_next_x;
  int atlas_next_y;
  int atlas_row_height;

  GlyphCacheEntry cache[GLYPH_CACHE_SIZE];
  size_t cache_count;
};

/* ========================================================================
 * Default Fonts by Platform
 * ======================================================================== */

static const char *DEFAULT_FONTS[] = {
    /* macOS (Ventura+ uses .ttc/.ttf; older releases shipped .dfont) */
    "/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/SFNSMono.ttf",
    "/System/Library/Fonts/Monaco.ttf", "/System/Library/Fonts/Courier.ttc",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    "/System/Library/Fonts/Supplemental/Andale Mono.ttf",
    "/System/Library/Fonts/Courier.dfont", "/System/Library/Fonts/Monaco.dfont",
    "/Library/Fonts/Courier New.ttf",

    /* Linux */
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",

    /* Windows (if run under WSL/Cygwin) */
    "C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf",

    nullptr /* Sentinel */
};

/* Outline symbol faces for codepoints absent from the mono fonts.
 * Apple Color Emoji is sbix/PNG; this FreeType build has PNG disabled, so
 * it does not rasterize. Apple Symbols has an outline for U+231A WATCH. */
static const char *SYMBOL_FONTS[] = {
    "/System/Library/Fonts/Apple Symbols.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
    "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansSymbols2-Regular.otf",
    "/usr/share/fonts/truetype/ancient-scripts/Symbola.ttf",
    "/usr/share/fonts/TTF/Symbola.ttf",

    nullptr /* Sentinel */
};

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

static bool font_file_exists(const char *path) {
  FILE *f;

  if (!path || !path[0]) {
    return false;
  }
  f = fopen(path, "r");
  if (!f) {
    return false;
  }
  fclose(f);
  return true;
}

static TTF_Font *open_face_index(const char *path, long face_index,
                                 int font_size, bool bold) {
  TTF_Font *font;

  if (!path) {
    return nullptr;
  }
  font = TTF_OpenFontIndex(path, font_size, face_index);
  if (!font) {
    return nullptr;
  }
  TTF_SetFontKerning(font, 0);
  if (bold) {
    TTF_SetFontStyle(font, TTF_STYLE_BOLD);
  }
  return font;
}

static TTF_Font *open_face(const char *path, int font_size, bool bold) {
  return open_face_index(path, 0, font_size, bold);
}

static bool family_names_equal(const char *left, const char *right) {
  if (!left || !right) {
    return false;
  }
  while (*left && *right) {
    unsigned char a = (unsigned char)*left++;
    unsigned char b = (unsigned char)*right++;

    if (a >= 'A' && a <= 'Z') {
      a = (unsigned char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
      b = (unsigned char)(b - 'A' + 'a');
    }
    if (a != b) {
      return false;
    }
  }
  return *left == '\0' && *right == '\0';
}

static bool font_matches(const FontMatch *match, TTF_Font *font,
                         bool require_codepoint, uint32_t codepoint) {
  const char *actual_family;
  const char *actual_style;

  actual_family = TTF_FontFaceFamilyName(font);
  actual_style = TTF_FontFaceStyleName(font);
  return (!match->family[0] ||
          family_names_equal(match->family, actual_family)) &&
         (!match->style[0] || family_names_equal(match->style, actual_style)) &&
         (!require_codepoint || TTF_GlyphIsProvided32(font, codepoint) != 0);
}

static TTF_Font *open_matching_face(const FontMatch *match, int font_size,
                                    bool require_codepoint, uint32_t codepoint,
                                    long *out_face_index) {
  TTF_Font *font;
  long face_count;

  if (!match || !out_face_index) {
    return nullptr;
  }
  if (match->face_index >= 0) {
    font = open_face_index(match->path, match->face_index, font_size, false);
    if (font &&
        (!require_codepoint || TTF_GlyphIsProvided32(font, codepoint) != 0)) {
      *out_face_index = match->face_index;
      return font;
    }
    if (font) {
      TTF_CloseFont(font);
    }
    return nullptr;
  }

  font = open_face_index(match->path, 0, font_size, false);
  if (!font) {
    return nullptr;
  }
  face_count = TTF_FontFaces(font);
  if (face_count < 1) {
    face_count = 1;
  }
  for (long i = 0; i < face_count; i++) {
    if (i > 0) {
      font = open_face_index(match->path, i, font_size, false);
      if (!font) {
        continue;
      }
    }
    if (font_matches(match, font, require_codepoint, codepoint)) {
      *out_face_index = i;
      return font;
    }
    TTF_CloseFont(font);
    font = nullptr;
  }
  return nullptr;
}

static TTF_Font *open_first_listed(const char *const *paths, int font_size,
                                   bool bold, const char *skip_path,
                                   const char **out_path, const char *kind) {
  for (int i = 0; paths[i] != nullptr; i++) {
    TTF_Font *font;

    if (skip_path && strcmp(paths[i], skip_path) == 0) {
      continue;
    }
    if (!font_file_exists(paths[i])) {
      continue;
    }
    font = open_face(paths[i], font_size, bold);
    if (font) {
      if (out_path) {
        *out_path = paths[i];
      }
      log_debug("Opened %s font: %s", kind, paths[i]);
      return font;
    }
    log_warn("Skipping %s font '%s': %s", kind, paths[i], TTF_GetError());
  }
  return nullptr;
}

static void close_fonts(FontManager *fm) {
  if (!fm) {
    return;
  }
  if (fm->font) {
    TTF_CloseFont(fm->font);
    fm->font = nullptr;
  }
  if (fm->font_bold) {
    TTF_CloseFont(fm->font_bold);
    fm->font_bold = nullptr;
  }
  if (fm->fallback) {
    TTF_CloseFont(fm->fallback);
    fm->fallback = nullptr;
  }
  if (fm->fallback_bold) {
    TTF_CloseFont(fm->fallback_bold);
    fm->fallback_bold = nullptr;
  }
  if (fm->symbols) {
    TTF_CloseFont(fm->symbols);
    fm->symbols = nullptr;
  }
  if (fm->symbols_bold) {
    TTF_CloseFont(fm->symbols_bold);
    fm->symbols_bold = nullptr;
  }
  for (size_t i = 0; i < fm->system_font_count; i++) {
    if (fm->system_fonts[i].font) {
      TTF_CloseFont(fm->system_fonts[i].font);
      fm->system_fonts[i].font = nullptr;
    }
    if (fm->system_fonts[i].font_bold) {
      TTF_CloseFont(fm->system_fonts[i].font_bold);
      fm->system_fonts[i].font_bold = nullptr;
    }
  }
  fm->system_font_count = 0;
  font_resolver_destroy(fm->resolver);
  fm->resolver = nullptr;
}

/* Reverse is resolved by the renderer before it asks for a glyph, so the
 * colors here are already the ones that should be drawn. */
static void apply_attributes(uint8_t attributes, uint32_t *fg_color,
                             uint32_t *bg_color) {
  if (attributes & ATTR_DIM) {
    *fg_color = RGB(RGB_RED(*fg_color) / 2, RGB_GREEN(*fg_color) / 2,
                    RGB_BLUE(*fg_color) / 2);
  }

  if (attributes & ATTR_CONCEALED) {
    *fg_color = *bg_color;
  }
}

/* ========================================================================
 * Glyph Cache (hash table → atlas region)
 * ======================================================================== */

static uint32_t glyph_cache_hash(uint32_t codepoint, uint32_t fg_color,
                                 bool bold, bool italic, uint8_t face) {
  uint32_t h = codepoint * 2654435761u;
  h ^= fg_color * 2246822519u;
  h ^= bold ? 3266489917u : 0;
  h ^= italic ? 668265263u : 0;
  h ^= (uint32_t)face * 4050765991u;
  return h & (GLYPH_CACHE_SIZE - 1);
}

static GlyphCacheEntry *glyph_cache_find(FontManager *fm, uint32_t codepoint,
                                         uint32_t fg_color, bool bold,
                                         bool italic, uint8_t face) {
  uint32_t idx = glyph_cache_hash(codepoint, fg_color, bold, italic, face);
  for (uint32_t i = 0; i < GLYPH_CACHE_SIZE; i++) {
    uint32_t probe = (idx + i) & (GLYPH_CACHE_SIZE - 1);
    GlyphCacheEntry *e = &fm->cache[probe];
    if (!e->occupied) {
      return nullptr;
    }
    if (e->codepoint == codepoint && e->fg_color == fg_color &&
        e->bold == bold && e->italic == italic && e->face == face) {
      return e;
    }
  }
  return nullptr;
}

static GlyphCacheEntry *glyph_cache_insert(FontManager *fm, uint32_t codepoint,
                                           uint32_t fg_color, bool bold,
                                           bool italic, uint8_t face,
                                           SDL_Rect src_rect, int w, int h) {
  uint32_t idx = glyph_cache_hash(codepoint, fg_color, bold, italic, face);
  for (uint32_t i = 0; i < GLYPH_CACHE_SIZE; i++) {
    uint32_t probe = (idx + i) & (GLYPH_CACHE_SIZE - 1);
    GlyphCacheEntry *e = &fm->cache[probe];
    if (!e->occupied) {
      e->codepoint = codepoint;
      e->fg_color = fg_color;
      e->bold = bold;
      e->italic = italic;
      e->face = face;
      e->occupied = true;
      e->src_rect = src_rect;
      e->width = w;
      e->height = h;
      fm->cache_count++;
      return e;
    }
  }
  return nullptr;
}

/* ========================================================================
 * Atlas helpers
 * ======================================================================== */

/** Wipe the atlas texture to fully transparent and reset the packing cursor. */
static void atlas_reset(FontManager *fm) {
  fm->atlas_next_x = 0;
  fm->atlas_next_y = 0;
  fm->atlas_row_height = 0;

  if (fm->atlas) {
    SDL_SetRenderTarget(fm->sdl_renderer, fm->atlas);
    SDL_SetRenderDrawColor(fm->sdl_renderer, 0, 0, 0, 0);
    SDL_RenderClear(fm->sdl_renderer);
    SDL_SetRenderTarget(fm->sdl_renderer, nullptr);
  }
}

/** Create the atlas texture.  Returns true on success. */
static bool atlas_create(FontManager *fm) {
  fm->atlas = SDL_CreateTexture(fm->sdl_renderer, SDL_PIXELFORMAT_RGBA8888,
                                SDL_TEXTUREACCESS_TARGET, GLYPH_ATLAS_SIZE,
                                GLYPH_ATLAS_SIZE);
  if (!fm->atlas) {
    fprintf(stderr, "ERROR: Failed to create glyph atlas: %s\n",
            SDL_GetError());
    return false;
  }

  SDL_SetTextureBlendMode(fm->atlas, SDL_BLENDMODE_BLEND);
  atlas_reset(fm);

  log_debug("Glyph atlas created: %dx%d", GLYPH_ATLAS_SIZE, GLYPH_ATLAS_SIZE);
  return true;
}

/**
 * Copy an already-rasterised glyph surface into the atlas.
 * Returns true on success and fills *out_src with the atlas region.
 */
static bool atlas_add_glyph(FontManager *fm, SDL_Surface *surface,
                            SDL_Rect *out_src) {
  int gw = surface->w;
  int gh = surface->h;

  /* Wrap to next row if this glyph doesn't fit horizontally */
  if (fm->atlas_next_x + gw > GLYPH_ATLAS_SIZE) {
    fm->atlas_next_y += fm->atlas_row_height;
    fm->atlas_next_x = 0;
    fm->atlas_row_height = 0;
  }

  /* Atlas full vertically → clear everything and start over.
   * This will cause one frame of re-caching but should be extremely
   * rare (requires >3000 unique glyph+color combinations). */
  if (fm->atlas_next_y + gh > GLYPH_ATLAS_SIZE) {
    log_debug("Glyph atlas full, resetting");
    font_manager_clear_cache(fm);
    /* After clear, atlas_next_x/y are 0 again */
  }

  /* Upload via a temporary texture so we can blit into the atlas */
  SDL_Texture *tmp = SDL_CreateTextureFromSurface(fm->sdl_renderer, surface);
  if (!tmp) {
    return false;
  }

  SDL_SetTextureBlendMode(tmp, SDL_BLENDMODE_NONE);

  SDL_Rect dst = {fm->atlas_next_x, fm->atlas_next_y, gw, gh};

  SDL_SetRenderTarget(fm->sdl_renderer, fm->atlas);
  SDL_RenderCopy(fm->sdl_renderer, tmp, nullptr, &dst);
  SDL_SetRenderTarget(fm->sdl_renderer, nullptr);

  SDL_DestroyTexture(tmp);

  *out_src = dst;

  /* Advance packing cursor */
  fm->atlas_next_x += gw;
  if (gh > fm->atlas_row_height) {
    fm->atlas_row_height = gh;
  }

  return true;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

FontManager *font_manager_init(const char *font_family, const char *font_path,
                               const char *fallback_path, int font_size,
                               SDL_Renderer *sdl_renderer) {
  log_trace("Initializing font manager");

  static bool ttf_initialized = false;
  if (!ttf_initialized) {
    if (TTF_Init() < 0) {
      fprintf(stderr, "ERROR: TTF_Init failed: %s\n", TTF_GetError());
      return nullptr;
    }
    ttf_initialized = true;
    log_debug("SDL_ttf initialized");
  }

  FontManager *fm = checked_calloc(1, sizeof(FontManager));
  if (!fm) {
    return nullptr;
  }

  FontMatch primary_match = {0};
  const char *font_to_load = font_path;
  long primary_index = 0;

  fm->resolver = font_resolver_init(font_family);
  if (font_to_load && font_to_load[0]) {
    fm->font = open_face(font_to_load, font_size, false);
    if (!fm->font) {
      fprintf(stderr, "ERROR: TTF_OpenFont failed for '%s': %s\n", font_to_load,
              TTF_GetError());
      close_fonts(fm);
      free(fm);
      return nullptr;
    }
  } else if (font_resolver_primary(fm->resolver, &primary_match)) {
    font_to_load = primary_match.path;
    fm->font =
        open_matching_face(&primary_match, font_size, false, 0, &primary_index);
  }
  if (!fm->font) {
    primary_index = 0;
    fm->font = open_first_listed(DEFAULT_FONTS, font_size, false, nullptr,
                                 &font_to_load, "default");
    if (!fm->font) {
      fprintf(stderr, "ERROR: No suitable default font found\n");
      close_fonts(fm);
      free(fm);
      return nullptr;
    }
  }

  fm->font_size = font_size;
  fm->sdl_renderer = sdl_renderer;
  fm->font_bold = open_face_index(font_to_load, primary_index, font_size, true);

  {
    const char *fb_path = nullptr;
    if (fallback_path && fallback_path[0]) {
      fb_path = fallback_path;
      fm->fallback_explicit = true;
      fm->fallback = open_face(fb_path, font_size, false);
      if (!fm->fallback) {
        log_warn("Failed to load fallback font '%s': %s", fb_path,
                 TTF_GetError());
      }
    } else {
      fm->fallback = open_first_listed(DEFAULT_FONTS, font_size, false,
                                       font_to_load, &fb_path, "default");
    }
    if (fm->fallback) {
      fm->fallback_bold = open_face(fb_path, font_size, true);
      log_debug("Fallback font loaded: %s (bold: %s)", fb_path ? fb_path : "?",
                fm->fallback_bold ? "yes" : "no");
    }
  }

  {
    const char *sym_path = nullptr;
    fm->symbols = open_first_listed(SYMBOL_FONTS, font_size, false, nullptr,
                                    &sym_path, "symbol");
    if (fm->symbols) {
      fm->symbols_bold = open_face(sym_path, font_size, true);
    }
  }

  int w, h;
  if (TTF_SizeText(fm->font, "M", &w, &h) < 0) {
    fprintf(stderr, "ERROR: TTF_SizeText failed: %s\n", TTF_GetError());
    close_fonts(fm);
    free(fm);
    return nullptr;
  }

  fm->char_width = w;
  fm->char_height = h;

  /* Create the atlas texture */
  if (!atlas_create(fm)) {
    close_fonts(fm);
    free(fm);
    return nullptr;
  }

  log_debug("Font loaded: %s, size=%d, char=%dx%d (bold: %s)", font_to_load,
            font_size, w, h, fm->font_bold ? "yes" : "no");

  return fm;
}

void font_manager_destroy(FontManager *fm) {
  if (!fm) {
    return;
  }

  log_trace("Destroying font manager");

  if (fm->atlas) {
    SDL_DestroyTexture(fm->atlas);
  }
  close_fonts(fm);

  free(fm);
}

static TTF_Font *face_for_bold(TTF_Font *regular, TTF_Font *bold_face,
                               bool bold) {
  if (bold && bold_face) {
    return bold_face;
  }
  return regular;
}

static bool fallback_miss_contains(const FontManager *fm, uint32_t codepoint) {
  size_t slot = (size_t)codepoint % FALLBACK_MISS_CACHE_SIZE;
  const FallbackMiss *miss = &fm->fallback_misses[slot];

  return miss->occupied && miss->codepoint == codepoint;
}

static void fallback_miss_add(FontManager *fm, uint32_t codepoint) {
  size_t slot = (size_t)codepoint % FALLBACK_MISS_CACHE_SIZE;

  fm->fallback_misses[slot].codepoint = codepoint;
  fm->fallback_misses[slot].occupied = true;
}

static bool system_face_matches(const SystemFontFace *face,
                                const FontMatch *match) {
  return face->match.face_index == match->face_index &&
         strcmp(face->match.path, match->path) == 0 &&
         strcmp(face->match.family, match->family) == 0 &&
         strcmp(face->match.style, match->style) == 0;
}

static TTF_Font *system_font_for_codepoint(FontManager *fm, uint32_t codepoint,
                                           bool bold, uint8_t *face) {
  FontMatch match = {0};
  SystemFontFace *system_face;
  long face_index;

  if (fallback_miss_contains(fm, codepoint) ||
      !font_resolver_fallback(fm->resolver, codepoint, &match)) {
    fallback_miss_add(fm, codepoint);
    return nullptr;
  }

  for (size_t i = 0; i < fm->system_font_count; i++) {
    system_face = &fm->system_fonts[i];
    if (system_face_matches(system_face, &match) &&
        TTF_GlyphIsProvided32(system_face->font, codepoint) != 0) {
      *face = (uint8_t)(GLYPH_FACE_SYSTEM_BASE + i);
      return face_for_bold(system_face->font, system_face->font_bold, bold);
    }
  }
  if (fm->system_font_count >= SYSTEM_FONT_CACHE_SIZE) {
    fallback_miss_add(fm, codepoint);
    return nullptr;
  }

  system_face = &fm->system_fonts[fm->system_font_count];
  system_face->font =
      open_matching_face(&match, fm->font_size, true, codepoint, &face_index);
  if (!system_face->font) {
    fallback_miss_add(fm, codepoint);
    return nullptr;
  }
  system_face->match = match;
  system_face->font_bold =
      open_face_index(match.path, face_index, fm->font_size, true);
  *face = (uint8_t)(GLYPH_FACE_SYSTEM_BASE + fm->system_font_count);
  fm->system_font_count++;
  log_debug("System fallback loaded: %s (face %ld)", match.path, face_index);
  return face_for_bold(system_face->font, system_face->font_bold, bold);
}

static TTF_Font *select_raster_font(FontManager *fm, uint32_t codepoint,
                                    bool bold, uint8_t *face) {
  TTF_Font *primary = face_for_bold(fm->font, fm->font_bold, bold);
  TTF_Font *system_font;

  if (TTF_GlyphIsProvided32(fm->font, codepoint) != 0) {
    *face = GLYPH_FACE_PRIMARY;
    return primary;
  }
  if (fm->fallback_explicit && fm->fallback &&
      TTF_GlyphIsProvided32(fm->fallback, codepoint) != 0) {
    *face = GLYPH_FACE_FALLBACK;
    return face_for_bold(fm->fallback, fm->fallback_bold, bold);
  }
  system_font = system_font_for_codepoint(fm, codepoint, bold, face);
  if (system_font) {
    return system_font;
  }
  if (fm->fallback && TTF_GlyphIsProvided32(fm->fallback, codepoint) != 0) {
    *face = GLYPH_FACE_FALLBACK;
    return face_for_bold(fm->fallback, fm->fallback_bold, bold);
  }
  if (fm->symbols && TTF_GlyphIsProvided32(fm->symbols, codepoint) != 0) {
    *face = GLYPH_FACE_SYMBOLS;
    return face_for_bold(fm->symbols, fm->symbols_bold, bold);
  }
  *face = (uint8_t)GLYPH_FACE_PRIMARY;
  return primary;
}

static SDL_Surface *render_glyph_surface(TTF_Font *font, uint32_t codepoint,
                                         SDL_Color fg, bool italic) {
  char utf8_str[5];
  int prev_style;
  SDL_Surface *surface;

  codepoint_to_utf8(codepoint, utf8_str);
  prev_style = TTF_GetFontStyle(font);
  if (italic) {
    TTF_SetFontStyle(font, prev_style | TTF_STYLE_ITALIC);
  }
  surface = TTF_RenderUTF8_Blended(font, utf8_str, fg);
  if (!surface) {
    surface = TTF_RenderUTF8_Blended(font, "?", fg);
  }
  if (italic) {
    TTF_SetFontStyle(font, prev_style);
  }
  return surface;
}

SDL_Texture *font_manager_get_glyph(FontManager *fm, uint32_t codepoint,
                                    uint32_t fg_color, uint32_t bg_color,
                                    uint8_t attributes, SDL_Rect *out_src,
                                    int *out_width, int *out_height) {
  bool bold;
  bool italic;
  uint8_t face = GLYPH_FACE_PRIMARY;
  GlyphCacheEntry *entry;
  TTF_Font *font;
  SDL_Color fg;
  SDL_Surface *surface;
  SDL_Rect src_rect;
  int gw;
  int gh;

  if (!fm || !fm->font || !fm->atlas) {
    return nullptr;
  }

  apply_attributes(attributes, &fg_color, &bg_color);
  bold = (attributes & ATTR_BOLD) != 0;
  italic = (attributes & ATTR_ITALIC) != 0;
  font = select_raster_font(fm, codepoint, bold, &face);

  entry = glyph_cache_find(fm, codepoint, fg_color, bold, italic, face);
  if (entry) {
    if (out_src) {
      *out_src = entry->src_rect;
    }
    if (out_width) {
      *out_width = entry->width;
    }
    if (out_height) {
      *out_height = entry->height;
    }
    return fm->atlas;
  }

  if (fm->cache_count >= GLYPH_CACHE_MAX_LOAD) {
    log_debug("Glyph cache full (%zu entries), clearing", fm->cache_count);
    font_manager_clear_cache(fm);
  }

  fg = (SDL_Color){.r = RGB_RED(fg_color),
                   .g = RGB_GREEN(fg_color),
                   .b = RGB_BLUE(fg_color),
                   .a = 255};

  surface = render_glyph_surface(font, codepoint, fg, italic);
  if (!surface) {
    return nullptr;
  }

  if (!atlas_add_glyph(fm, surface, &src_rect)) {
    SDL_FreeSurface(surface);
    return nullptr;
  }

  gw = surface->w;
  gh = surface->h;
  SDL_FreeSurface(surface);

  glyph_cache_insert(fm, codepoint, fg_color, bold, italic, face, src_rect, gw,
                     gh);

  if (out_src) {
    *out_src = src_rect;
  }
  if (out_width) {
    *out_width = gw;
  }
  if (out_height) {
    *out_height = gh;
  }
  return fm->atlas;
}

void font_manager_clear_cache(FontManager *fm) {
  if (!fm) {
    return;
  }

  for (size_t i = 0; i < GLYPH_CACHE_SIZE; i++) {
    fm->cache[i].occupied = false;
  }
  fm->cache_count = 0;

  atlas_reset(fm);
}

void font_manager_get_char_size(FontManager *fm, int *width, int *height) {
  if (!fm) {
    if (width)
      *width = 0;
    if (height)
      *height = 0;
    return;
  }

  if (width) {
    *width = fm->char_width;
  }
  if (height) {
    *height = fm->char_height;
  }
}
