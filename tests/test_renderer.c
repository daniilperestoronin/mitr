/**
 * test_renderer.c - Smoke tests for SDL renderer
 */

#include "renderer.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include <SDL2/SDL.h>
#include <stdlib.h>

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

TEST(renderer_smoke_init_destroy) {
  if (!sdl_video_ready()) {
    return;
  }

  SDL_Window *window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_HIDDEN);
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

  Renderer *renderer = renderer_init(window, sdl_renderer, font_manager);
  ASSERT_NOT_NULL(renderer);

  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);
  terminal_buffer_put_char(tb, 'A');
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));

  /* Unique glyphs pack the atlas (SetRenderTarget atlas, then window).
   * renderer_get_glyph must rebind the retained frame afterwards. */
  for (char c = 'B'; c <= 'Z'; c++) {
    terminal_buffer_put_char(tb, (uint32_t)(unsigned char)c);
  }
  renderer_set_search_prompt(renderer, "xyz");
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  renderer_set_search_prompt(renderer, nullptr);

  /* Second present must succeed against the retained frame (or window
   * fallback). Dirty bits stay clear when the grid did not change. */
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));

  renderer_force_full_redraw(renderer);
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));

  terminal_buffer_put_char(tb, 'B');
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 0));
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));

  size_t rows = 0;
  size_t cols = 0;
  renderer_get_term_size(renderer, &rows, &cols);
  ASSERT_TRUE(rows > 0);
  ASSERT_TRUE(cols > 0);

  int drawable_w = 0;
  int drawable_h = 0;
  int logical_w = 0;
  int logical_h = 0;
  int win_w = 0;
  int win_h = 0;
  int out_w = 0;
  int out_h = 0;
  font_manager_get_char_size(font_manager, &drawable_w, &drawable_h);
  renderer_get_logical_cell_size(renderer, &logical_w, &logical_h);
  ASSERT_TRUE(logical_w > 0);
  ASSERT_TRUE(logical_h > 0);
  SDL_GetWindowSize(window, &win_w, &win_h);
  if (SDL_GetRendererOutputSize(sdl_renderer, &out_w, &out_h) != 0 ||
      out_w <= 0 || out_h <= 0) {
    out_w = win_w;
    out_h = win_h;
  }
  ASSERT_EQ(logical_w, mitr_drawable_to_logical(
                           drawable_w, mitr_display_scale(win_w, out_w)));
  ASSERT_EQ(logical_h, mitr_drawable_to_logical(
                           drawable_h, mitr_display_scale(win_h, out_h)));

  renderer_set_appearance(renderer, 2, 2, RGB(192, 192, 192),
                          CURSOR_STYLE_BLOCK, RGB(100, 150, 255));
  renderer_compute_grid(renderer, win_w, win_h, &rows, &cols);
  ASSERT_TRUE(cols >= 2);
  ASSERT_TRUE(rows >= 2);
  {
    int usable_w = win_w - 4;
    size_t expect_cols = (logical_w > 0) ? (size_t)(usable_w / logical_w) : 2;
    if (expect_cols < 2) {
      expect_cols = 2;
    }
    if (expect_cols > MITR_MAX_COLS) {
      expect_cols = MITR_MAX_COLS;
    }
    ASSERT_EQ(cols, expect_cols);
  }

  SDL_SetWindowSize(window, 800, 600);
  SDL_GetWindowSize(window, &win_w, &win_h);
  renderer_update_size(renderer, win_w, win_h);
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));

  terminal_buffer_destroy(tb);
  renderer_destroy(renderer);
  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

TEST(renderer_put_render_clears_dirty) {
  if (!sdl_video_ready()) {
    return;
  }

  SDL_Window *window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_HIDDEN);
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

  Renderer *renderer = renderer_init(window, sdl_renderer, font_manager);
  ASSERT_NOT_NULL(renderer);

  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_put_char(tb, 'A');
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 0));
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));

  terminal_buffer_put_char(tb, 'B');
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));

  renderer_force_full_redraw(renderer);
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));

  terminal_buffer_scroll_up(tb);
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));

  terminal_buffer_destroy(tb);
  renderer_destroy(renderer);
  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

TEST(renderer_render_consumes_scroll_shift) {
  size_t top = 0;
  size_t bottom = 0;
  int delta = 0;

  if (!sdl_video_ready()) {
    return;
  }

  SDL_Window *window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_HIDDEN);
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

  Renderer *renderer = renderer_init(window, sdl_renderer, font_manager);
  ASSERT_NOT_NULL(renderer);

  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);

  /* Present clears obsolete scroll-damage metadata after painting. */
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));

  terminal_buffer_set_cursor_visible(tb, false);
  terminal_buffer_scroll_up(tb);
  renderer_render(renderer, tb, nullptr);
  ASSERT_FALSE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));

  terminal_buffer_scroll_up(tb);
  ASSERT_TRUE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_EQ(delta, 1);

  terminal_buffer_destroy(tb);
  renderer_destroy(renderer);
  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

TEST(renderer_force_full_redraw_null_is_noop) {
  renderer_force_full_redraw(nullptr);
}

TEST(renderer_cell_pitch_uses_logical_not_ttf) {
  const int drawable_glyph = 19;
  const float scale = 2.0f;
  int logical;
  int pitch;

  /* No Retina required: 19px TTF glyph at scale 2 is logical 10, pitch 20.
   * Grid/mouse use the logical cell; render stepping must use pitch, not 19. */
  logical = mitr_drawable_to_logical(drawable_glyph, scale);
  pitch = renderer_cell_pitch(logical, scale);

  ASSERT_EQ(logical, 10);
  ASSERT_EQ(pitch, mitr_logical_to_drawable(logical, scale));
  ASSERT_EQ(pitch, 20);
  ASSERT_NE(pitch, drawable_glyph);
}

static bool renderer_read_rgb(SDL_Renderer *sdl, int x, int y, uint8_t *red,
                              uint8_t *green, uint8_t *blue) {
  Uint32 pixel = 0;
  SDL_Rect rect = {x, y, 1, 1};
  SDL_PixelFormat *fmt;

  if (SDL_RenderReadPixels(sdl, &rect, SDL_PIXELFORMAT_RGBA8888, &pixel,
                           (int)sizeof(pixel)) != 0) {
    return false;
  }
  fmt = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA8888);
  if (!fmt) {
    return false;
  }
  SDL_GetRGB(pixel, fmt, red, green, blue);
  SDL_FreeFormat(fmt);
  return true;
}

TEST(renderer_repaints_clean_rows_every_frame) {
  const uint32_t first_bg = RGB(255, 0, 0);
  const uint32_t second_bg = RGB(0, 0, 255);
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  TermCell *cell;

  if (!sdl_video_ready()) {
    return;
  }

  SDL_Window *window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 160, 80, SDL_WINDOW_HIDDEN);
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

  Renderer *renderer = renderer_init(window, sdl_renderer, font_manager);
  ASSERT_NOT_NULL(renderer);
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor_visible(tb, false);

  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_NOT_NULL(cell);
  cell->codepoint = ' ';
  cell->width = 1;
  cell->bg_color = first_bg;
  renderer_render(renderer, tb, nullptr);
  ASSERT_TRUE(renderer_read_rgb(sdl_renderer, 1, 1, &red, &green, &blue));
  ASSERT_EQ(red, 255);
  ASSERT_EQ(green, 0);
  ASSERT_EQ(blue, 0);

  /* A frame is always reconstructed from TerminalBuffer. Dirty rows are
   * scheduling metadata, not retained-pixel correctness state. */
  cell->bg_color = second_bg;
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  renderer_render(renderer, tb, nullptr);
  ASSERT_TRUE(renderer_read_rgb(sdl_renderer, 1, 1, &red, &green, &blue));
  ASSERT_EQ(red, 0);
  ASSERT_EQ(green, 0);
  ASSERT_EQ(blue, 255);

  terminal_buffer_destroy(tb);
  renderer_destroy(renderer);
  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

/* SGR 7 swaps the painted colors. DECSCNM swaps too, so both together
 * leave the cell's own colors. A reversed space is the Helix block cursor. */
TEST(renderer_sgr_reverse_swaps_painted_colors) {
  const uint32_t fg = RGB(255, 0, 0);
  const uint32_t bg = RGB(0, 0, 255);
  int logical_w = 0;
  int logical_h = 0;
  int win_w = 0;
  int win_h = 0;
  int out_w = 0;
  int out_h = 0;
  int pitch_w;
  int pitch_h;
  int col_x;
  int top_y;
  int line_y;
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  TermCell *reversed;
  TermCell *plain;

  if (!sdl_video_ready()) {
    return;
  }

  SDL_Window *window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_HIDDEN);
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

  Renderer *renderer = renderer_init(window, sdl_renderer, font_manager);
  ASSERT_NOT_NULL(renderer);

  TerminalBuffer *tb = terminal_buffer_init(8, 20);
  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor_visible(tb, false);

  reversed = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_NOT_NULL(reversed);
  reversed->codepoint = ' ';
  reversed->width = 1;
  reversed->fg_color = fg;
  reversed->bg_color = bg;
  reversed->attributes = (uint8_t)(ATTR_REVERSE | ATTR_UNDERLINE);

  plain = terminal_buffer_get_cell(tb, 0, 4);
  ASSERT_NOT_NULL(plain);
  plain->codepoint = ' ';
  plain->width = 1;
  plain->fg_color = fg;
  plain->bg_color = bg;
  plain->attributes = ATTR_NONE;
  terminal_buffer_mark_row_dirty(tb, 0);

  renderer_get_logical_cell_size(renderer, &logical_w, &logical_h);
  SDL_GetWindowSize(window, &win_w, &win_h);
  if (SDL_GetRendererOutputSize(sdl_renderer, &out_w, &out_h) != 0 ||
      out_w <= 0 || out_h <= 0) {
    out_w = win_w;
    out_h = win_h;
  }
  pitch_w = renderer_cell_pitch(logical_w, mitr_display_scale(win_w, out_w));
  pitch_h = renderer_cell_pitch(logical_h, mitr_display_scale(win_h, out_h));
  ASSERT_TRUE(pitch_w > 1);
  ASSERT_TRUE(pitch_h > 3);

  renderer_render(renderer, tb, nullptr);

  col_x = 2 * pitch_w + pitch_w / 2;
  top_y = 1;
  line_y = pitch_h - 2;
  ASSERT_TRUE(
      renderer_read_rgb(sdl_renderer, col_x, top_y, &red, &green, &blue));
  ASSERT_EQ(red, 255);
  ASSERT_EQ(green, 0);
  ASSERT_EQ(blue, 0);
  ASSERT_TRUE(
      renderer_read_rgb(sdl_renderer, col_x, line_y, &red, &green, &blue));
  ASSERT_EQ(red, 0);
  ASSERT_EQ(green, 0);
  ASSERT_EQ(blue, 255);

  col_x = 4 * pitch_w + pitch_w / 2;
  ASSERT_TRUE(
      renderer_read_rgb(sdl_renderer, col_x, top_y, &red, &green, &blue));
  ASSERT_EQ(red, 0);
  ASSERT_EQ(green, 0);
  ASSERT_EQ(blue, 255);

  /* Both reverses cancel: the reversed cell keeps its own background. */
  terminal_buffer_set_reverse_screen(tb, true);
  renderer_render(renderer, tb, nullptr);
  col_x = 2 * pitch_w + pitch_w / 2;
  ASSERT_TRUE(
      renderer_read_rgb(sdl_renderer, col_x, top_y, &red, &green, &blue));
  ASSERT_EQ(red, 0);
  ASSERT_EQ(green, 0);
  ASSERT_EQ(blue, 255);
  col_x = 4 * pitch_w + pitch_w / 2;
  ASSERT_TRUE(
      renderer_read_rgb(sdl_renderer, col_x, top_y, &red, &green, &blue));
  ASSERT_EQ(red, 255);
  ASSERT_EQ(green, 0);
  ASSERT_EQ(blue, 0);

  terminal_buffer_destroy(tb);
  renderer_destroy(renderer);
  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

TEST(renderer_bottom_remainder_uses_terminal_background) {
  const uint32_t terminal_bg = RGB(0, 0, 0);
  const uint32_t status_bg = RGB(0, 255, 255);
  int logical_w = 0;
  int logical_h = 0;
  int win_w = 0;
  int win_h = 0;
  int out_w = 0;
  int out_h = 0;
  int pitch_h;
  size_t rows = 0;
  size_t cols = 0;
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;

  if (!sdl_video_ready()) {
    return;
  }

  SDL_Window *window =
      SDL_CreateWindow("mitr-test", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, 160, 80, SDL_WINDOW_HIDDEN);
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

  Renderer *renderer = renderer_init(window, sdl_renderer, font_manager);
  ASSERT_NOT_NULL(renderer);
  renderer_get_logical_cell_size(renderer, &logical_w, &logical_h);
  ASSERT_TRUE(logical_w > 0);
  ASSERT_TRUE(logical_h > 0);

  SDL_SetWindowSize(window, logical_w * 8 + 3, logical_h * 2 + 3);
  SDL_GetWindowSize(window, &win_w, &win_h);
  renderer_update_size(renderer, win_w, win_h);
  renderer_compute_grid(renderer, win_w, win_h, &rows, &cols);
  ASSERT_EQ(rows, 2);

  TerminalBuffer *tb = terminal_buffer_init(rows, cols);
  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_colors(tb, RGB(255, 255, 255), terminal_bg);
  terminal_buffer_clear(tb);
  terminal_buffer_set_cursor_visible(tb, false);
  for (size_t col = 0; col < cols; col++) {
    TermCell *cell = terminal_buffer_get_cell(tb, rows - 1, col);
    ASSERT_NOT_NULL(cell);
    cell->bg_color = status_bg;
  }

  if (SDL_GetRendererOutputSize(sdl_renderer, &out_w, &out_h) != 0 ||
      out_w <= 0 || out_h <= 0) {
    out_w = win_w;
    out_h = win_h;
  }
  pitch_h = renderer_cell_pitch(logical_h, mitr_display_scale(win_h, out_h));
  ASSERT_TRUE((int)(rows * (size_t)pitch_h) < out_h);

  renderer_render(renderer, tb, nullptr);
  ASSERT_TRUE(
      renderer_read_rgb(sdl_renderer, 1, pitch_h + 1, &red, &green, &blue));
  ASSERT_EQ(red, 0);
  ASSERT_EQ(green, 255);
  ASSERT_EQ(blue, 255);
  ASSERT_TRUE(
      renderer_read_rgb(sdl_renderer, 1, out_h - 1, &red, &green, &blue));
  ASSERT_EQ(red, 0);
  ASSERT_EQ(green, 0);
  ASSERT_EQ(blue, 0);

  terminal_buffer_destroy(tb);
  renderer_destroy(renderer);
  font_manager_destroy(font_manager);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

void run_renderer_tests(void) {
  printf("\n" TERM_COLOR_YELLOW "=== Renderer Smoke Tests ===" TERM_COLOR_RESET
         "\n");
  RUN_TEST(renderer_smoke_init_destroy);
  RUN_TEST(renderer_put_render_clears_dirty);
  RUN_TEST(renderer_render_consumes_scroll_shift);
  RUN_TEST(renderer_force_full_redraw_null_is_noop);
  RUN_TEST(renderer_cell_pitch_uses_logical_not_ttf);
  RUN_TEST(renderer_repaints_clean_rows_every_frame);
  RUN_TEST(renderer_sgr_reverse_swaps_painted_colors);
  RUN_TEST(renderer_bottom_remainder_uses_terminal_background);
}
