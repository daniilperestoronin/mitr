/**
 * renderer.c - Renderer implementation
 *
 * Two-pass rendering: backgrounds first (batched), then foreground glyphs
 * (using cached textures from font_manager). Every presented frame is fully
 * rebuilt in a retained drawable-sized target texture, then copied to the
 * window. The same path is used on every SDL backend. Window fallback also
 * paints every row.
 */

#include "renderer.h"

struct Renderer {
  SDL_Window *window;
  SDL_Renderer *renderer;
  FontManager *font_manager;

  int window_width;
  int window_height;
  int drawable_width;
  int drawable_height;
  float scale;
  int char_width;
  int char_height;
  int cell_width;
  int cell_height;

  bool cursor_visible;

  int padding_x;
  int padding_y;
  uint32_t cursor_color;
  CursorStyle cursor_style;
  uint32_t selection_color;

  bool search_hit_enabled;
  size_t search_hit_row;
  size_t search_hit_col;
  size_t search_hit_ncells;

  bool search_prompt_enabled;
  char search_prompt[256];

  SDL_Texture *frame;
  bool frame_bound;
  bool rebind_failed;
};

static int renderer_output_w(const Renderer *r) {
  return r->drawable_width > 0 ? r->drawable_width : r->window_width;
}

static int renderer_output_h(const Renderer *r) {
  return r->drawable_height > 0 ? r->drawable_height : r->window_height;
}

static int renderer_origin_x(const Renderer *r) {
  return mitr_logical_to_drawable(r->padding_x, r->scale);
}

static int renderer_origin_y(const Renderer *r) {
  return mitr_logical_to_drawable(r->padding_y, r->scale);
}

static void renderer_sync_display(Renderer *r, int window_w, int window_h) {
  int dw = 0;
  int dh = 0;

  r->window_width = window_w;
  r->window_height = window_h;
  if (SDL_GetRendererOutputSize(r->renderer, &dw, &dh) != 0 || dw <= 0 ||
      dh <= 0) {
    dw = window_w;
    dh = window_h;
  }
  r->drawable_width = dw;
  r->drawable_height = dh;
  r->scale = mitr_display_scale(r->window_width, r->drawable_width);
  r->cell_width = mitr_drawable_to_logical(r->char_width, r->scale);
  r->cell_height = mitr_drawable_to_logical(r->char_height, r->scale);
  if (r->cell_width < 1) {
    r->cell_width = 1;
  }
  if (r->cell_height < 1) {
    r->cell_height = 1;
  }
}

static void renderer_destroy_frame(Renderer *r) {
  if (!r || !r->frame) {
    return;
  }
  if (r->frame_bound) {
    SDL_SetRenderTarget(r->renderer, nullptr);
    r->frame_bound = false;
  }
  SDL_DestroyTexture(r->frame);
  r->frame = nullptr;
}

static bool renderer_create_frame(Renderer *r) {
  int w;
  int h;

  renderer_destroy_frame(r);

  w = renderer_output_w(r);
  h = renderer_output_h(r);
  if (w <= 0 || h <= 0) {
    return false;
  }
  if (!SDL_RenderTargetSupported(r->renderer)) {
    return false;
  }

  r->frame = SDL_CreateTexture(r->renderer, SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_TARGET, w, h);
  if (!r->frame) {
    log_warn("Retained frame texture failed (%dx%d): %s", w, h, SDL_GetError());
    return false;
  }

  SDL_SetTextureBlendMode(r->frame, SDL_BLENDMODE_NONE);
  log_debug("Retained frame texture %dx%d", w, h);
  return true;
}

static bool renderer_bind_frame(Renderer *r) {
  if (!r->frame) {
    return false;
  }
  if (SDL_SetRenderTarget(r->renderer, r->frame) != 0) {
    renderer_destroy_frame(r);
    return false;
  }
  r->frame_bound = true;
  return true;
}

/* Glyph atlas packing resets the SDL target to the window; rebind the frame. */
static bool renderer_rebind_frame(Renderer *r) {
  if (!r->frame_bound || !r->frame) {
    return true;
  }
  if (SDL_SetRenderTarget(r->renderer, r->frame) != 0) {
    log_warn("Retained frame rebind failed: %s", SDL_GetError());
    renderer_destroy_frame(r);
    r->rebind_failed = true;
    return false;
  }
  return true;
}

static SDL_Texture *renderer_get_glyph(Renderer *r, uint32_t codepoint,
                                       uint32_t fg, uint32_t bg,
                                       uint8_t attributes, SDL_Rect *src,
                                       int *gw, int *gh) {
  SDL_Texture *atlas = font_manager_get_glyph(r->font_manager, codepoint, fg,
                                              bg, attributes, src, gw, gh);
  (void)renderer_rebind_frame(r);
  return atlas;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

Renderer *renderer_init(SDL_Window *window, SDL_Renderer *sdl_renderer,
                        FontManager *font_manager) {
  log_trace("Initializing renderer");

  if (!window || !sdl_renderer || !font_manager) {
    fprintf(stderr, "ERROR: Invalid parameters to renderer_init\n");
    return nullptr;
  }

  Renderer *r = checked_calloc(1, sizeof(Renderer));
  if (!r) {
    return nullptr;
  }

  r->window = window;
  r->renderer = sdl_renderer;
  r->font_manager = font_manager;

  SDL_GetWindowSize(window, &r->window_width, &r->window_height);
  font_manager_get_char_size(font_manager, &r->char_width, &r->char_height);
  renderer_sync_display(r, r->window_width, r->window_height);
  (void)renderer_create_frame(r);

  r->cursor_visible = true;
  r->padding_x = 0;
  r->padding_y = 0;
  r->cursor_color = RGB(192, 192, 192);
  r->cursor_style = CURSOR_STYLE_BLOCK;
  r->selection_color = RGB(100, 150, 255);

  log_debug("Renderer initialized: window=%dx%d drawable=%dx%d scale=%.2f "
            "char=%dx%d cell=%dx%d",
            r->window_width, r->window_height, r->drawable_width,
            r->drawable_height, (double)r->scale, r->char_width, r->char_height,
            r->cell_width, r->cell_height);

  return r;
}

void renderer_set_appearance(Renderer *r, int padding_x, int padding_y,
                             uint32_t cursor_color, CursorStyle cursor_style,
                             uint32_t selection_color) {
  if (!r)
    return;
  r->padding_x = padding_x < 0 ? 0 : padding_x;
  r->padding_y = padding_y < 0 ? 0 : padding_y;
  r->cursor_color = cursor_color;
  r->cursor_style = cursor_style;
  r->selection_color = selection_color;
}

void renderer_compute_grid(const Renderer *r, int pixel_w, int pixel_h,
                           size_t *rows, size_t *cols) {
  int cell_w;
  int cell_h;
  int usable_w;
  int usable_h;
  size_t c;
  size_t rw;

  if (!r || !rows || !cols)
    return;

  cell_w = r->cell_width > 0 ? r->cell_width : r->char_width;
  cell_h = r->cell_height > 0 ? r->cell_height : r->char_height;

  usable_w = pixel_w - (r->padding_x * 2);
  usable_h = pixel_h - (r->padding_y * 2);
  if (usable_w < 0)
    usable_w = 0;
  if (usable_h < 0)
    usable_h = 0;

  c = (cell_w > 0) ? (size_t)(usable_w / cell_w) : 2;
  rw = (cell_h > 0) ? (size_t)(usable_h / cell_h) : 2;
  if (c < 2)
    c = 2;
  if (rw < 2)
    rw = 2;
  if (c > MITR_MAX_COLS)
    c = MITR_MAX_COLS;
  if (rw > MITR_MAX_ROWS)
    rw = MITR_MAX_ROWS;
  *cols = c;
  *rows = rw;
}

void renderer_get_logical_cell_size(const Renderer *r, int *width,
                                    int *height) {
  if (!r) {
    if (width)
      *width = 0;
    if (height)
      *height = 0;
    return;
  }
  if (width) {
    *width = r->cell_width;
  }
  if (height) {
    *height = r->cell_height;
  }
}

int renderer_cell_pitch(int logical_px, float scale) {
  int pitch = mitr_logical_to_drawable(logical_px, scale);
  if (pitch < 1) {
    return 1;
  }
  return pitch;
}

void renderer_destroy(Renderer *r) {
  if (!r) {
    return;
  }

  log_trace("Destroying renderer");
  renderer_destroy_frame(r);
  free(r);
}

void renderer_get_term_size(Renderer *r, size_t *rows, size_t *cols) {
  if (!r) {
    if (rows)
      *rows = 0;
    if (cols)
      *cols = 0;
    return;
  }

  if (rows) {
    *rows =
        (r->cell_height > 0) ? (size_t)(r->window_height / r->cell_height) : 0;
  }
  if (cols) {
    *cols = (r->cell_width > 0) ? (size_t)(r->window_width / r->cell_width) : 0;
  }
}

void renderer_set_search_hit(Renderer *r, bool enabled, size_t view_row,
                             size_t view_col, size_t ncells) {
  if (!r) {
    return;
  }
  r->search_hit_enabled = enabled;
  r->search_hit_row = view_row;
  r->search_hit_col = view_col;
  r->search_hit_ncells = ncells;
}

void renderer_set_search_prompt(Renderer *r, const char *text_or_null) {
  size_t n;

  if (!r) {
    return;
  }
  if (!text_or_null) {
    r->search_prompt_enabled = false;
    r->search_prompt[0] = '\0';
    return;
  }
  r->search_prompt_enabled = true;
  n = strlen(text_or_null);
  if (n >= sizeof(r->search_prompt)) {
    n = sizeof(r->search_prompt) - 1;
  }
  memcpy(r->search_prompt, text_or_null, n);
  r->search_prompt[n] = '\0';
}

void renderer_update_size(Renderer *r, int new_width, int new_height) {
  if (!r) {
    return;
  }

  renderer_sync_display(r, new_width, new_height);
  (void)renderer_create_frame(r);

  log_debug("Renderer size updated: window=%dx%d drawable=%dx%d chars~=%dx%d",
            new_width, new_height, r->drawable_width, r->drawable_height,
            r->cell_width > 0 ? new_width / r->cell_width : 0,
            r->cell_height > 0 ? new_height / r->cell_height : 0);
}

void renderer_force_full_redraw(Renderer *r) { (void)r; }

/**
 * Colors a cell is painted with. SGR reverse and DECSCNM each swap fg and
 * bg; together they cancel, so a reversed cell on a reversed screen looks
 * like the cell's own colors.
 */
static void cell_effective_colors(const TermCell *cell, bool reverse_screen,
                                  uint32_t *out_fg, uint32_t *out_bg) {
  bool cell_reverse = (cell->attributes & ATTR_REVERSE) != 0;

  if (reverse_screen ^ cell_reverse) {
    *out_fg = cell->bg_color;
    *out_bg = cell->fg_color;
  } else {
    *out_fg = cell->fg_color;
    *out_bg = cell->bg_color;
  }
}

static inline uint32_t cell_effective_bg(const TermCell *cell,
                                         bool reverse_screen) {
  uint32_t fg = 0;
  uint32_t bg = 0;

  cell_effective_colors(cell, reverse_screen, &fg, &bg);
  return bg;
}

static size_t renderer_utf8_next(const char *s, uint32_t *cp) {
  const unsigned char *p = (const unsigned char *)s;
  if (p[0] == 0) {
    return 0;
  }
  if (p[0] < 0x80U) {
    *cp = p[0];
    return 1;
  }
  if ((p[0] & 0xE0U) == 0xC0U && (p[1] & 0xC0U) == 0x80U) {
    *cp = ((uint32_t)(p[0] & 0x1FU) << 6) | (uint32_t)(p[1] & 0x3FU);
    return 2;
  }
  if ((p[0] & 0xF0U) == 0xE0U && (p[1] & 0xC0U) == 0x80U &&
      (p[2] & 0xC0U) == 0x80U) {
    *cp = ((uint32_t)(p[0] & 0x0FU) << 12) | ((uint32_t)(p[1] & 0x3FU) << 6) |
          (uint32_t)(p[2] & 0x3FU);
    return 3;
  }
  if ((p[0] & 0xF8U) == 0xF0U && (p[1] & 0xC0U) == 0x80U &&
      (p[2] & 0xC0U) == 0x80U && (p[3] & 0xC0U) == 0x80U) {
    *cp = ((uint32_t)(p[0] & 0x07U) << 18) | ((uint32_t)(p[1] & 0x3FU) << 12) |
          ((uint32_t)(p[2] & 0x3FU) << 6) | (uint32_t)(p[3] & 0x3FU);
    return 4;
  }
  *cp = p[0];
  return 1;
}

/* Center (or clip) a glyph dest-rect inside the logical-cell drawable box. */
static SDL_Rect renderer_glyph_dest(int cell_x, int cell_y, int pitch_w,
                                    int pitch_h, int gw, int gh) {
  int dw = (gw > 0) ? gw : pitch_w;
  int dh = (gh > 0) ? gh : pitch_h;
  int ox;
  int oy;

  if (dw > pitch_w) {
    dw = pitch_w;
  }
  if (dh > pitch_h) {
    dh = pitch_h;
  }
  ox = (pitch_w - dw) / 2;
  oy = (pitch_h - dh) / 2;
  return (SDL_Rect){cell_x + ox, cell_y + oy, dw, dh};
}

/* One-line query bar at the bottom of the window (search overlay). */
static void renderer_draw_search_prompt(Renderer *r) {
  static const char prefix[] = "Search: ";
  const char *parts[2];
  uint32_t fg = COLOR_BRIGHT_WHITE;
  uint32_t bg = r->selection_color;
  int out_h = renderer_output_h(r);
  int out_w = renderer_output_w(r);
  int pitch_w = renderer_cell_pitch(r->cell_width, r->scale);
  int pitch_h = renderer_cell_pitch(r->cell_height, r->scale);
  int y = out_h - pitch_h;
  int x = renderer_origin_x(r);
  SDL_Rect bar;

  if (!r->search_prompt_enabled) {
    return;
  }
  if (y < 0) {
    y = 0;
  }
  bar = (SDL_Rect){0, y, out_w, pitch_h};
  SDL_SetRenderDrawColor(r->renderer, RGB_RED(bg), RGB_GREEN(bg), RGB_BLUE(bg),
                         255);
  SDL_RenderFillRect(r->renderer, &bar);

  parts[0] = prefix;
  parts[1] = r->search_prompt;
  for (size_t i = 0; i < 2; i++) {
    const char *p = parts[i];
    while (*p != '\0' && x < out_w) {
      uint32_t cp = 0;
      size_t n = renderer_utf8_next(p, &cp);
      SDL_Rect src;
      int gw = 0;
      int gh = 0;
      SDL_Texture *atlas;

      if (n == 0) {
        break;
      }
      p += n;
      if (cp == ' ' || cp == 0) {
        x += pitch_w;
        continue;
      }
      atlas = renderer_get_glyph(r, cp, fg, bg, ATTR_NONE, &src, &gw, &gh);
      if (atlas) {
        SDL_Rect dest = renderer_glyph_dest(x, y, pitch_w, pitch_h, gw, gh);
        SDL_RenderCopy(r->renderer, atlas, &src, &dest);
      }
      x += pitch_w;
    }
  }
}

static void renderer_fill_strip(Renderer *r, int y, int h, uint32_t color) {
  SDL_Rect rect = {0, y, renderer_output_w(r), h};
  SDL_SetRenderDrawColor(r->renderer, RGB_RED(color), RGB_GREEN(color),
                         RGB_BLUE(color), 255);
  SDL_RenderFillRect(r->renderer, &rect);
}

static bool cell_is_search_hit(const Renderer *r, size_t row, size_t col) {
  return r->search_hit_enabled && row == r->search_hit_row &&
         col >= r->search_hit_col &&
         (col - r->search_hit_col) < r->search_hit_ncells;
}

static void
renderer_draw_row_background(Renderer *r, const TermCell *row_cells, size_t row,
                             size_t cols, int y, int cw, int ch, int origin_x,
                             uint32_t config_bg, bool reverse_screen,
                             bool has_selection, const MouseManager *mm) {
  size_t run_start = 0;
  uint32_t run_color = 0;
  bool run_is_selected = false;
  bool in_run = false;
  int grid_right;
  uint32_t edge_bg;

  renderer_fill_strip(r, y, ch, config_bg);

  for (size_t col = 0; col <= cols; col++) {
    uint32_t bg = config_bg;
    bool selected = false;
    bool extends = false;

    if (col < cols) {
      bg = cell_effective_bg(&row_cells[col], reverse_screen);
      selected = has_selection && mouse_manager_is_selected(mm, row, col);
      if (cell_is_search_hit(r, row, col)) {
        selected = true;
      }
    }

    if (col < cols && in_run && selected == run_is_selected &&
        (selected || bg == run_color)) {
      extends = true;
    }

    if (extends) {
      continue;
    }

    if (in_run) {
      int rx = origin_x + (int)(run_start * (size_t)cw);
      int rw = (int)((col - run_start) * (size_t)cw);
      SDL_Rect rect = {rx, y, rw, ch};
      if (run_is_selected) {
        SDL_SetRenderDrawColor(r->renderer, RGB_RED(r->selection_color),
                               RGB_GREEN(r->selection_color),
                               RGB_BLUE(r->selection_color), 255);
        SDL_RenderFillRect(r->renderer, &rect);
      } else if (run_color != config_bg) {
        SDL_SetRenderDrawColor(r->renderer, RGB_RED(run_color),
                               RGB_GREEN(run_color), RGB_BLUE(run_color), 255);
        SDL_RenderFillRect(r->renderer, &rect);
      }
    }

    if (col < cols && (selected || bg != config_bg)) {
      run_start = col;
      run_color = bg;
      run_is_selected = selected;
      in_run = true;
    } else {
      in_run = false;
    }
  }

  grid_right = origin_x + (int)(cols * (size_t)cw);
  if (grid_right >= renderer_output_w(r) || cols == 0) {
    return;
  }
  edge_bg = cell_effective_bg(&row_cells[cols - 1], reverse_screen);
  if (edge_bg == config_bg) {
    return;
  }
  SDL_SetRenderDrawColor(r->renderer, RGB_RED(edge_bg), RGB_GREEN(edge_bg),
                         RGB_BLUE(edge_bg), 255);
  SDL_Rect gap = {grid_right, y, renderer_output_w(r) - grid_right, ch};
  SDL_RenderFillRect(r->renderer, &gap);
}

static void renderer_draw_combining(Renderer *r, const TermCell *row_cells,
                                    size_t cols, size_t start_col, int x, int y,
                                    int pitch_w, int pitch_h,
                                    bool reverse_screen) {
  size_t next_col = start_col;
  while (next_col < cols) {
    const TermCell *comb = &row_cells[next_col];
    SDL_Rect src;
    int gw = 0;
    int gh = 0;
    SDL_Texture *atlas;
    uint32_t c_fg;
    uint32_t c_bg;

    if (comb->width != 0 || (comb->flags & CELL_FLAG_CONTINUATION) != 0) {
      break;
    }
    if (comb->codepoint != 0 && comb->codepoint != ' ') {
      cell_effective_colors(comb, reverse_screen, &c_fg, &c_bg);
      atlas = renderer_get_glyph(r, comb->codepoint, c_fg, c_bg,
                                 comb->attributes, &src, &gw, &gh);
      if (atlas) {
        SDL_Rect dest = renderer_glyph_dest(x, y, pitch_w, pitch_h, gw, gh);
        SDL_RenderCopy(r->renderer, atlas, &src, &dest);
      }
    }
    next_col++;
  }
}

static void renderer_draw_row_glyphs(Renderer *r, const TermCell *row_cells,
                                     size_t cols, int y, int cw, int ch,
                                     int origin_x, bool reverse_screen) {
  for (size_t col = 0; col < cols; col++) {
    const TermCell *cell = &row_cells[col];
    bool has_glyph;
    bool has_underline;
    bool has_strike;
    int x;
    int cell_w;
    uint32_t fg;
    uint32_t bg;

    if ((cell->flags & CELL_FLAG_CONTINUATION) != 0 || cell->width == 0) {
      continue;
    }

    has_glyph = cell->codepoint != ' ' && cell->codepoint != 0;
    has_underline = (cell->attributes & ATTR_UNDERLINE) != 0;
    has_strike = (cell->attributes & ATTR_STRIKE) != 0;
    if (!has_glyph && !has_underline && !has_strike) {
      continue;
    }

    x = origin_x + (int)(col * (size_t)cw);
    cell_w = (cell->width == 2) ? cw * 2 : cw;
    cell_effective_colors(cell, reverse_screen, &fg, &bg);

    if (has_glyph) {
      SDL_Rect src;
      int gw = 0;
      int gh = 0;
      SDL_Texture *atlas;

      atlas = renderer_get_glyph(r, cell->codepoint, fg, bg, cell->attributes,
                                 &src, &gw, &gh);
      if (atlas) {
        SDL_Rect dest = renderer_glyph_dest(x, y, cell_w, ch, gw, gh);
        SDL_RenderCopy(r->renderer, atlas, &src, &dest);
      }
      renderer_draw_combining(r, row_cells, cols,
                              col + (cell->width == 2 ? 2u : 1u), x, y, cell_w,
                              ch, reverse_screen);
    }

    if (has_underline) {
      SDL_SetRenderDrawColor(r->renderer, RGB_RED(fg), RGB_GREEN(fg),
                             RGB_BLUE(fg), 255);
      SDL_RenderDrawLine(r->renderer, x, y + ch - 2, x + cell_w - 1,
                         y + ch - 2);
    }

    if (has_strike) {
      SDL_SetRenderDrawColor(r->renderer, RGB_RED(fg), RGB_GREEN(fg),
                             RGB_BLUE(fg), 255);
      SDL_RenderDrawLine(r->renderer, x, y + ch / 2, x + cell_w - 1,
                         y + ch / 2);
    }
  }
}

static void renderer_draw_cursor(Renderer *r, TerminalBuffer *tb, size_t cols,
                                 int origin_x, int origin_y, int cw, int ch) {
  size_t cursor_row;
  size_t cursor_col;
  int cx;
  int cy;
  int cursor_w = cw;
  const TermCell *cursor_row_cells;
  SDL_Rect cursor_rect;

  terminal_buffer_get_cursor(tb, &cursor_row, &cursor_col);
  cx = origin_x + (int)(cursor_col * (size_t)cw);
  cy = origin_y + (int)(cursor_row * (size_t)ch);
  cursor_row_cells = terminal_buffer_get_view_row(tb, cursor_row);
  if (cursor_row_cells && cursor_col < cols &&
      cursor_row_cells[cursor_col].width == 2) {
    cursor_w = cw * 2;
  }
  cursor_rect = (SDL_Rect){.x = cx, .y = cy, .w = cursor_w, .h = ch};

  switch (r->cursor_style) {
  case CURSOR_STYLE_UNDERLINE:
    cursor_rect.y = cy + ch - 2;
    cursor_rect.h = 2;
    break;
  case CURSOR_STYLE_BAR:
    cursor_rect.w = 2;
    break;
  case CURSOR_STYLE_BLOCK:
  default:
    break;
  }

  SDL_SetRenderDrawBlendMode(r->renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r->renderer, RGB_RED(r->cursor_color),
                         RGB_GREEN(r->cursor_color), RGB_BLUE(r->cursor_color),
                         180);
  SDL_RenderFillRect(r->renderer, &cursor_rect);
  SDL_SetRenderDrawBlendMode(r->renderer, SDL_BLENDMODE_NONE);
}

static uint32_t renderer_clear_color(TerminalBuffer *tb) {
  return terminal_buffer_is_reverse_screen(tb)
             ? terminal_buffer_get_config_fg(tb)
             : terminal_buffer_get_config_bg(tb);
}

static void renderer_clear(Renderer *r, uint32_t color) {
  SDL_SetRenderDrawColor(r->renderer, RGB_RED(color), RGB_GREEN(color),
                         RGB_BLUE(color), 255);
  SDL_RenderClear(r->renderer);
}

static bool renderer_cursor_shown(const Renderer *r, const TerminalBuffer *tb) {
  return terminal_buffer_is_cursor_visible(tb) && r->cursor_visible &&
         terminal_buffer_get_scroll_offset(tb) == 0;
}

static bool renderer_paint_view_row_cells(Renderer *r, TerminalBuffer *tb,
                                          const MouseManager *mm, size_t row) {
  const TermCell *row_cells;
  bool reverse_screen;
  uint32_t config_bg;
  int cw;
  int ch;
  size_t cols;
  int origin_x;
  int origin_y;
  int y;
  bool has_selection;

  if (!r || r->rebind_failed) {
    return false;
  }
  row_cells = terminal_buffer_get_view_row(tb, row);
  if (!row_cells) {
    return false;
  }
  reverse_screen = terminal_buffer_is_reverse_screen(tb);
  config_bg = renderer_clear_color(tb);
  cw = renderer_cell_pitch(r->cell_width, r->scale);
  ch = renderer_cell_pitch(r->cell_height, r->scale);
  cols = terminal_buffer_get_cols(tb);
  origin_x = renderer_origin_x(r);
  origin_y = renderer_origin_y(r);
  y = origin_y + (int)(row * (size_t)ch);
  has_selection = mm && mouse_manager_is_selecting(mm);
  renderer_draw_row_background(r, row_cells, row, cols, y, cw, ch, origin_x,
                               config_bg, reverse_screen, has_selection, mm);
  renderer_draw_row_glyphs(r, row_cells, cols, y, cw, ch, origin_x,
                           reverse_screen);
  return true;
}

static void renderer_paint_grid(Renderer *r, TerminalBuffer *tb,
                                const MouseManager *mm, bool show_cursor) {
  int cw = renderer_cell_pitch(r->cell_width, r->scale);
  int ch = renderer_cell_pitch(r->cell_height, r->scale);
  size_t rows = terminal_buffer_get_rows(tb);
  size_t cols = terminal_buffer_get_cols(tb);
  int origin_x = renderer_origin_x(r);
  int origin_y = renderer_origin_y(r);

  for (size_t row = 0; row < rows; row++) {
    if (r->rebind_failed) {
      return;
    }
    (void)renderer_paint_view_row_cells(r, tb, mm, row);
  }

  if (r->rebind_failed) {
    return;
  }
  if (show_cursor) {
    renderer_draw_cursor(r, tb, cols, origin_x, origin_y, cw, ch);
  }
  renderer_draw_search_prompt(r);
}

static bool renderer_blit_frame(Renderer *r) {
  if (SDL_SetRenderTarget(r->renderer, nullptr) != 0) {
    log_warn("Retained frame unbind failed: %s", SDL_GetError());
    return false;
  }
  r->frame_bound = false;
  if (SDL_SetTextureBlendMode(r->frame, SDL_BLENDMODE_NONE) != 0) {
    log_warn("Retained frame blend mode failed: %s", SDL_GetError());
    return false;
  }
  if (SDL_RenderCopy(r->renderer, r->frame, nullptr, nullptr) != 0) {
    log_warn("Retained frame blit failed: %s", SDL_GetError());
    return false;
  }
  return true;
}

static void renderer_paint_window_fallback(Renderer *r, TerminalBuffer *tb,
                                           const MouseManager *mm) {
  if (r->frame_bound) {
    SDL_SetRenderTarget(r->renderer, nullptr);
    r->frame_bound = false;
  }
  renderer_destroy_frame(r);
  r->rebind_failed = false;
  renderer_clear(r, renderer_clear_color(tb));
  renderer_paint_grid(r, tb, mm, renderer_cursor_shown(r, tb));
}

void renderer_render(Renderer *r, TerminalBuffer *tb, const MouseManager *mm) {
  bool used_frame;
  bool show_cursor;

  if (!r || !tb) {
    return;
  }

  /*
   * Rebuild the complete retained frame for every Present. This keeps the
   * rendering model identical across Metal, OpenGL, OpenGL ES, and software
   * backends and never relies on preserved pixels or dirty-row bookkeeping.
   */
  r->cursor_visible = true;
  r->rebind_failed = false;
  show_cursor = renderer_cursor_shown(r, tb);

  used_frame = renderer_bind_frame(r);
  r->frame_bound = used_frame;
  renderer_clear(r, renderer_clear_color(tb));
  renderer_paint_grid(r, tb, mm, show_cursor);

  if (r->rebind_failed || (used_frame && !r->frame)) {
    renderer_paint_window_fallback(r, tb, mm);
  } else if (r->frame_bound && r->frame) {
    if (!renderer_blit_frame(r)) {
      renderer_paint_window_fallback(r, tb, mm);
    }
  }

  SDL_RenderPresent(r->renderer);
  terminal_buffer_clear_dirty(tb);
}
