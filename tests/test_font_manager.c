/**
 * test_font_manager.c - Smoke tests for font manager
 */

#include "font_manager.h"
#include "test_framework.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TEST_FONTS[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/SFNSMono.ttf",
    "/System/Library/Fonts/Monaco.ttf",
    "/System/Library/Fonts/Courier.ttc",
    "/System/Library/Fonts/Supplemental/Courier New.ttf",
    "/System/Library/Fonts/Courier.dfont",
    "/System/Library/Fonts/Monaco.dfont",
    "/Library/Fonts/Courier New.ttf",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "/Library/Fonts/Arial Unicode.ttf",
    "/System/Library/Fonts/Supplemental/AppleGothic.ttf",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    nullptr,
};

static const uint32_t FALLBACK_PROBE_CPS[] = {
    0x4E00, /* CJK 一 */
    0x3042, /* Hiragana あ */
    0x0416, /* Cyrillic Ж */
    0x03B1, /* Greek α */
    0x05D0, /* Hebrew א */
    0xFF01, /* fullwidth ! */
};

static bool sdl_video_ready(void) {
  if (setenv("SDL_VIDEODRIVER", "dummy", 1) != 0) {
    return false;
  }
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    SDL_Quit();
    return false;
  }
  return true;
}

static bool font_file_readable(const char *path) {
  FILE *f;

  if (!path) {
    return false;
  }
  f = fopen(path, "r");
  if (!f) {
    return false;
  }
  fclose(f);
  return true;
}

TEST(font_manager_smoke_init_destroy) {
  if (!sdl_video_ready()) {
    return;
  }

  SDL_Window *window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 320, 240, SDL_WINDOW_HIDDEN);
  if (!window) {
    SDL_Quit();
    return;
  }

  SDL_Renderer *sdl_renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!sdl_renderer) {
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  FontManager *font_manager =
      font_manager_init(nullptr, nullptr, nullptr, 16, sdl_renderer);
  if (!font_manager) {
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  int width = 0;
  int height = 0;
  font_manager_get_char_size(font_manager, &width, &height);
  ASSERT_TRUE(width > 0);
  ASSERT_TRUE(height > 0);

  SDL_Rect src = {0};
  int glyph_w = 0;
  int glyph_h = 0;
  SDL_Texture *atlas = font_manager_get_glyph(
      font_manager, 'X', COLOR_WHITE, COLOR_BLACK, 0, &src, &glyph_w, &glyph_h);
  ASSERT_NOT_NULL(atlas);
  ASSERT_TRUE(glyph_w > 0);
  ASSERT_TRUE(glyph_h > 0);

  atlas = font_manager_get_glyph(font_manager, 'I', COLOR_WHITE, COLOR_BLACK,
                                 ATTR_ITALIC, &src, &glyph_w, &glyph_h);
  ASSERT_NOT_NULL(atlas);
  ASSERT_TRUE(glyph_w > 0);
  ASSERT_TRUE(glyph_h > 0);

  atlas =
      font_manager_get_glyph(font_manager, 'B', COLOR_WHITE, COLOR_BLACK,
                             ATTR_BOLD | ATTR_ITALIC, &src, &glyph_w, &glyph_h);
  ASSERT_NOT_NULL(atlas);
  ASSERT_TRUE(glyph_w > 0);
  ASSERT_TRUE(glyph_h > 0);

  font_manager_clear_cache(font_manager);
  font_manager_destroy(font_manager);
  font_manager_destroy(nullptr);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

TEST(font_manager_missing_fallback_does_not_crash) {
  SDL_Window *window;
  SDL_Renderer *sdl_renderer;
  FontManager *font_manager;
  SDL_Rect src = {0};
  int glyph_w = 0;
  int glyph_h = 0;
  SDL_Texture *atlas;

  if (!sdl_video_ready()) {
    return;
  }

  window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 320, 240, SDL_WINDOW_HIDDEN);
  if (!window) {
    SDL_Quit();
    return;
  }

  sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!sdl_renderer) {
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  font_manager = font_manager_init(
      nullptr, nullptr, "/no/such/mitr-fallback.ttf", 16, sdl_renderer);
  if (!font_manager) {
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  atlas = font_manager_get_glyph(font_manager, 'A', COLOR_WHITE, COLOR_BLACK, 0,
                                 &src, &glyph_w, &glyph_h);
  ASSERT_NOT_NULL(atlas);
  ASSERT_TRUE(glyph_w > 0);

  atlas = font_manager_get_glyph(font_manager, 0x4E00, COLOR_WHITE, COLOR_BLACK,
                                 0, &src, &glyph_w, &glyph_h);
  /* Missing CJK may be '?' / .notdef; must not crash. */
  (void)atlas;

  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

TEST(font_manager_fallback_rasterizes_missing_glyph) {
  SDL_Window *window;
  SDL_Renderer *sdl_renderer;
  FontManager *font_manager;
  const char *primary = nullptr;
  const char *fallback = nullptr;
  uint32_t probe = 0;
  SDL_Rect src = {0};
  int glyph_w = 0;
  int glyph_h = 0;
  SDL_Texture *atlas;

  if (!sdl_video_ready()) {
    return;
  }

  window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 320, 240, SDL_WINDOW_HIDDEN);
  if (!window) {
    SDL_Quit();
    return;
  }

  sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!sdl_renderer) {
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  if (TTF_Init() < 0) {
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  for (int i = 0; TEST_FONTS[i] != nullptr && probe == 0; i++) {
    TTF_Font *a;

    if (!font_file_readable(TEST_FONTS[i])) {
      continue;
    }
    a = TTF_OpenFont(TEST_FONTS[i], 16);
    if (!a) {
      continue;
    }
    for (int j = 0; TEST_FONTS[j] != nullptr && probe == 0; j++) {
      TTF_Font *b;

      if (i == j || !font_file_readable(TEST_FONTS[j])) {
        continue;
      }
      b = TTF_OpenFont(TEST_FONTS[j], 16);
      if (!b) {
        continue;
      }
      for (size_t k = 0; k < ARRAY_SIZE(FALLBACK_PROBE_CPS); k++) {
        uint32_t cp = FALLBACK_PROBE_CPS[k];
        if (TTF_GlyphIsProvided32(a, cp) == 0 &&
            TTF_GlyphIsProvided32(b, cp) != 0) {
          primary = TEST_FONTS[i];
          fallback = TEST_FONTS[j];
          probe = cp;
          break;
        }
      }
      TTF_CloseFont(b);
    }
    TTF_CloseFont(a);
  }

  if (probe == 0 || !primary || !fallback) {
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  font_manager =
      font_manager_init(nullptr, primary, fallback, 16, sdl_renderer);
  if (!font_manager) {
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  atlas = font_manager_get_glyph(font_manager, probe, COLOR_WHITE, COLOR_BLACK,
                                 0, &src, &glyph_w, &glyph_h);
  ASSERT_NOT_NULL(atlas);
  ASSERT_TRUE(glyph_w > 0);
  ASSERT_TRUE(glyph_h > 0);

  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

static void test_utf8(uint32_t cp, char out[5]) {
  if (cp < 0x80u) {
    out[0] = (char)cp;
    out[1] = '\0';
  } else if (cp < 0x800u) {
    out[0] = (char)(0xC0u | (cp >> 6));
    out[1] = (char)(0x80u | (cp & 0x3Fu));
    out[2] = '\0';
  } else if (cp < 0x10000u) {
    out[0] = (char)(0xE0u | (cp >> 12));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    out[3] = '\0';
  } else {
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    out[4] = '\0';
  }
}

/* Ink in the middle of the bitmap. Menlo's .notdef box is hollow there;
 * Apple Symbols' U+231A WATCH is not. */
static int surface_center_opaque(SDL_Surface *surface) {
  int x0;
  int x1;
  int y0;
  int y1;
  int count = 0;

  if (!surface || surface->format->BytesPerPixel != 4 || surface->w < 4 ||
      surface->h < 4) {
    return -1;
  }
  x0 = surface->w / 4;
  x1 = surface->w - x0;
  y0 = surface->h / 4;
  y1 = surface->h - y0;
  if (SDL_LockSurface(surface) < 0) {
    return -1;
  }
  for (int y = y0; y < y1; y++) {
    const Uint8 *row =
        (const Uint8 *)surface->pixels + (size_t)y * (size_t)surface->pitch;
    for (int x = x0; x < x1; x++) {
      Uint32 px = *(const Uint32 *)(const void *)(row + (size_t)x * 4u);
      Uint8 r;
      Uint8 g;
      Uint8 b;
      Uint8 a;

      SDL_GetRGBA(px, surface->format, &r, &g, &b, &a);
      if (a > 16) {
        count++;
      }
    }
  }
  SDL_UnlockSurface(surface);
  return count;
}

static int font_center_opaque(const char *path, int size, uint32_t cp) {
  TTF_Font *font;
  SDL_Color fg = {255, 255, 255, 255};
  SDL_Surface *surface;
  char utf8[5];
  int count;

  font = TTF_OpenFont(path, size);
  if (!font) {
    return -1;
  }
  test_utf8(cp, utf8);
  surface = TTF_RenderUTF8_Blended(font, utf8, fg);
  TTF_CloseFont(font);
  if (!surface) {
    return -1;
  }
  count = surface_center_opaque(surface);
  SDL_FreeSurface(surface);
  return count;
}

static int atlas_center_opaque(SDL_Renderer *renderer, SDL_Texture *atlas,
                               SDL_Rect src) {
  SDL_Surface *shot;
  int count;

  if (src.w < 1 || src.h < 1) {
    return -1;
  }
  shot = SDL_CreateRGBSurfaceWithFormat(0, src.w, src.h, 32,
                                        SDL_PIXELFORMAT_ARGB8888);
  if (!shot) {
    return -1;
  }
  if (SDL_SetRenderTarget(renderer, atlas) < 0 ||
      SDL_RenderReadPixels(renderer, &src, SDL_PIXELFORMAT_ARGB8888,
                           shot->pixels, shot->pitch) < 0) {
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_FreeSurface(shot);
    return -1;
  }
  SDL_SetRenderTarget(renderer, nullptr);
  count = surface_center_opaque(shot);
  SDL_FreeSurface(shot);
  return count;
}

TEST(font_manager_watch_uses_system_fallback) {
  const int size = 32;
  const uint32_t watch = 0x231A;
  const char *primary_path = nullptr;
  SDL_Window *window;
  SDL_Renderer *sdl_renderer;
  FontManager *font_manager;
  SDL_Texture *atlas;
  SDL_Rect src = {0};
  int glyph_w = 0;
  int glyph_h = 0;
  int box_ink;
  int drawn;

  if (!sdl_video_ready() || TTF_Init() < 0) {
    return;
  }
  for (size_t i = 0; TEST_FONTS[i] != nullptr; i++) {
    TTF_Font *font;
    if (!font_file_readable(TEST_FONTS[i])) {
      continue;
    }
    font = TTF_OpenFont(TEST_FONTS[i], size);
    if (font && TTF_GlyphIsProvided32(font, watch) == 0) {
      primary_path = TEST_FONTS[i];
      TTF_CloseFont(font);
      break;
    }
    if (font) {
      TTF_CloseFont(font);
    }
  }
  if (!primary_path) {
    SDL_Quit();
    return;
  }
  box_ink = font_center_opaque(primary_path, size, watch);

  window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 320, 240, SDL_WINDOW_HIDDEN);
  if (!window) {
    SDL_Quit();
    return;
  }
  sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!sdl_renderer) {
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }
  font_manager =
      font_manager_init("monospace", primary_path, nullptr, size, sdl_renderer);
  if (!font_manager) {
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  atlas = font_manager_get_glyph(font_manager, watch, COLOR_WHITE, COLOR_BLACK,
                                 0, &src, &glyph_w, &glyph_h);
  ASSERT_NOT_NULL(atlas);
  drawn = atlas_center_opaque(sdl_renderer, atlas, src);
  ASSERT_TRUE(box_ink >= 0);
  ASSERT_TRUE(drawn > box_ink);

  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

TEST(font_manager_watch_uses_symbol_face) {
  static const char menlo_path[] = "/System/Library/Fonts/Menlo.ttc";
  static const char symbols_path[] = "/System/Library/Fonts/Apple Symbols.ttf";
  const int size = 32;
  const uint32_t watch = 0x231A;
  SDL_Window *window;
  SDL_Renderer *sdl_renderer;
  FontManager *font_manager;
  TTF_Font *menlo;
  TTF_Font *symbols;
  SDL_Texture *atlas;
  SDL_Rect src = {0};
  int glyph_w = 0;
  int glyph_h = 0;
  int box_ink;
  int watch_ink;
  int drawn;

  if (!sdl_video_ready() || !font_file_readable(menlo_path) ||
      !font_file_readable(symbols_path)) {
    return;
  }
  if (TTF_Init() < 0) {
    SDL_Quit();
    return;
  }
  menlo = TTF_OpenFont(menlo_path, size);
  symbols = TTF_OpenFont(symbols_path, size);
  if (!menlo || !symbols || TTF_GlyphIsProvided32(menlo, watch) != 0 ||
      TTF_GlyphIsProvided32(symbols, watch) == 0) {
    if (menlo) {
      TTF_CloseFont(menlo);
    }
    if (symbols) {
      TTF_CloseFont(symbols);
    }
    SDL_Quit();
    return;
  }
  TTF_CloseFont(menlo);
  TTF_CloseFont(symbols);

  box_ink = font_center_opaque(menlo_path, size, watch);
  watch_ink = font_center_opaque(symbols_path, size, watch);

  window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 320, 240, SDL_WINDOW_HIDDEN);
  if (!window) {
    SDL_Quit();
    return;
  }
  sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!sdl_renderer) {
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  /* Fallback is Menlo too, so it cannot supply U+231A. */
  font_manager =
      font_manager_init(nullptr, menlo_path, menlo_path, size, sdl_renderer);
  if (!font_manager) {
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return;
  }

  atlas = font_manager_get_glyph(font_manager, watch, COLOR_WHITE, COLOR_BLACK,
                                 0, &src, &glyph_w, &glyph_h);
  ASSERT_NOT_NULL(atlas);
  drawn = atlas_center_opaque(sdl_renderer, atlas, src);
  ASSERT_TRUE(box_ink >= 0);
  ASSERT_TRUE(watch_ink > box_ink);
  ASSERT_TRUE(drawn > box_ink);

  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

void run_font_manager_tests(void) {
  printf("\n" TERM_COLOR_YELLOW
         "=== Font Manager Smoke Tests ===" TERM_COLOR_RESET "\n");
  RUN_TEST(font_manager_smoke_init_destroy);
  RUN_TEST(font_manager_missing_fallback_does_not_crash);
  RUN_TEST(font_manager_fallback_rasterizes_missing_glyph);
  RUN_TEST(font_manager_watch_uses_system_fallback);
  RUN_TEST(font_manager_watch_uses_symbol_face);
}
