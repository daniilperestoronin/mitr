/**
 * terminal_buffer.c - Terminal buffer implementation
 */

#include "terminal_buffer.h"
#include <string.h>

#define HYPERLINK_INTERN_MAX 64
#define HYPERLINK_URI_MAX 2048
#define SEARCH_QUERY_MAX_CODEPOINTS 256

/* ========================================================================
 * Internal Structure
 * ======================================================================== */

struct TerminalBuffer {
  TermCell *cells;
  size_t rows;
  size_t cols;

  size_t cursor_row;
  size_t cursor_col;
  bool cursor_visible;
  bool pending_wrap;

  size_t cursor_saved_row;
  size_t cursor_saved_col;
  uint8_t cursor_saved_attr;
  uint32_t cursor_saved_fg;
  uint32_t cursor_saved_bg;

  size_t scroll_top;
  size_t scroll_bottom;

  bool origin_mode;
  bool auto_wrap;
  bool insert_mode;

  uint32_t default_fg;
  uint32_t default_bg;
  uint8_t current_attr;

  uint32_t config_fg;
  uint32_t config_bg;

  bool *tab_stops;

  bool reverse_screen;

  TermCell *alt_cells;
  bool using_alt_screen;
  size_t main_cursor_row;
  size_t main_cursor_col;
  uint32_t main_default_fg;
  uint32_t main_default_bg;
  uint8_t main_current_attr;

  TermCell *scrollback;
  size_t scrollback_capacity;
  size_t scrollback_used;
  size_t scrollback_start;
  size_t scroll_offset;

  uint16_t current_link_id;
  uint16_t link_uri_count;
  char *link_uris[HYPERLINK_INTERN_MAX];
  uint16_t *link_ids;
  uint16_t *alt_link_ids;
  uint16_t *scrollback_link_ids;

  uint8_t *dirty_rows;

  bool scroll_shift_active;
  size_t scroll_shift_top;
  size_t scroll_shift_bottom;
  int scroll_shift_delta;
};

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

/**
 * Get linear index from row/col
 */
static inline size_t get_index(TerminalBuffer *tb, size_t row, size_t col) {
  return row * tb->cols + col;
}

#define SCROLLBACK_DEFAULT_CAPACITY 1000

/**
 * Initialize a cell with default values
 */
static inline void init_cell(TermCell *cell, TerminalBuffer *tb) {
  cell->codepoint = ' ';
  cell->fg_color = tb->default_fg;
  cell->bg_color = tb->default_bg;
  cell->attributes = ATTR_NONE;
  cell->width = 1;
  cell->flags = 0;
}

static inline void set_cell_link_id(TerminalBuffer *tb, size_t row, size_t col,
                                    uint16_t id) {
  if (tb->link_ids) {
    tb->link_ids[row * tb->cols + col] = id;
  }
}

static inline void init_cell_at(TerminalBuffer *tb, size_t row, size_t col) {
  init_cell(&tb->cells[row * tb->cols + col], tb);
  set_cell_link_id(tb, row, col, 0);
}

static size_t view_offset(const TerminalBuffer *tb) {
  size_t offset = tb->scroll_offset;
  if (offset > tb->scrollback_used) {
    offset = tb->scrollback_used;
  }
  return offset;
}

static void mark_view_row_dirty(TerminalBuffer *tb, size_t view_row) {
  if (!tb || !tb->dirty_rows || view_row >= tb->rows) {
    return;
  }
  tb->dirty_rows[view_row] = 1;
}

static void mark_live_row_dirty(TerminalBuffer *tb, size_t live_row) {
  size_t offset;
  if (!tb || live_row >= tb->rows) {
    return;
  }
  offset = view_offset(tb);
  if (live_row + offset >= tb->rows) {
    return;
  }
  mark_view_row_dirty(tb, live_row + offset);
}

static void clear_pending_scroll_shift(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }
  tb->scroll_shift_active = false;
  tb->scroll_shift_top = 0;
  tb->scroll_shift_bottom = 0;
  tb->scroll_shift_delta = 0;
}

static void mark_all_visible_dirty(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }
  clear_pending_scroll_shift(tb);
  if (!tb->dirty_rows) {
    return;
  }
  memset(tb->dirty_rows, 1, tb->rows);
}

static int scroll_shift_abs(int delta) { return delta < 0 ? -delta : delta; }

static void mark_vacated_shift_rows(TerminalBuffer *tb, size_t top,
                                    size_t bottom, int delta) {
  int n = scroll_shift_abs(delta);
  size_t i;

  if (delta > 0) {
    for (i = 0; i < (size_t)n; i++) {
      mark_live_row_dirty(tb, bottom - i);
    }
  } else if (delta < 0) {
    for (i = 0; i < (size_t)n; i++) {
      mark_live_row_dirty(tb, top + i);
    }
  }
}

static bool scroll_shift_region_ok(const TerminalBuffer *tb, size_t top,
                                   size_t bottom, size_t n) {
  size_t height;

  if (!tb || view_offset(tb) != 0 || top > bottom || bottom >= tb->rows ||
      n == 0) {
    return false;
  }
  height = bottom - top + 1;
  return n < height;
}

static void record_scroll_shift(TerminalBuffer *tb, size_t top, size_t bottom,
                                int delta) {
  int combined;
  size_t n;

  if (!tb) {
    return;
  }
  n = (size_t)scroll_shift_abs(delta);
  if (!scroll_shift_region_ok(tb, top, bottom, n)) {
    mark_all_visible_dirty(tb);
    return;
  }
  if (tb->scroll_shift_active &&
      (tb->scroll_shift_top != top || tb->scroll_shift_bottom != bottom)) {
    mark_all_visible_dirty(tb);
    return;
  }
  combined = tb->scroll_shift_active ? tb->scroll_shift_delta + delta : delta;
  if (combined == 0) {
    mark_vacated_shift_rows(tb, top, bottom, delta);
    clear_pending_scroll_shift(tb);
    return;
  }
  if ((size_t)scroll_shift_abs(combined) >= (bottom - top + 1)) {
    mark_all_visible_dirty(tb);
    return;
  }
  tb->scroll_shift_active = true;
  tb->scroll_shift_top = top;
  tb->scroll_shift_bottom = bottom;
  tb->scroll_shift_delta = combined;
  mark_vacated_shift_rows(tb, top, bottom, combined);
}

void terminal_buffer_mark_all_dirty(TerminalBuffer *tb) {
  mark_all_visible_dirty(tb);
}

void terminal_buffer_mark_row_dirty(TerminalBuffer *tb, size_t live_row) {
  mark_live_row_dirty(tb, live_row);
}

bool terminal_buffer_row_is_dirty(const TerminalBuffer *tb, size_t view_row) {
  if (!tb || !tb->dirty_rows || view_row >= tb->rows) {
    return false;
  }
  return tb->dirty_rows[view_row] != 0;
}

void terminal_buffer_clear_dirty(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }
  clear_pending_scroll_shift(tb);
  if (!tb->dirty_rows) {
    return;
  }
  memset(tb->dirty_rows, 0, tb->rows);
}

bool terminal_buffer_take_scroll_shift(TerminalBuffer *tb, size_t *top,
                                       size_t *bottom, int *delta) {
  if (!tb || !tb->scroll_shift_active) {
    return false;
  }
  if (top) {
    *top = tb->scroll_shift_top;
  }
  if (bottom) {
    *bottom = tb->scroll_shift_bottom;
  }
  if (delta) {
    *delta = tb->scroll_shift_delta;
  }
  clear_pending_scroll_shift(tb);
  return true;
}

/** Fill every 8th column starting at 8 (VT default tab stops). */
static void init_default_tab_stops(bool *tab_stops, size_t cols) {
  for (size_t i = 0; i < cols; i++) {
    tab_stops[i] = false;
  }
  for (size_t i = 8; i < cols; i += 8) {
    tab_stops[i] = true;
  }
}

/**
 * Allocate a cell grid of rows*cols, initialized with defaults from tb.
 * Returns nullptr on overflow or allocation failure.
 */
static TermCell *alloc_cell_grid(TerminalBuffer *tb, size_t rows, size_t cols) {
  size_t cell_count;
  if (size_mul_overflow(rows, cols, &cell_count)) {
    log_warn("Cell grid size overflow: %zux%zu", rows, cols);
    return nullptr;
  }

  TermCell *cells = checked_calloc(cell_count, sizeof(TermCell));
  if (!cells) {
    return nullptr;
  }

  for (size_t i = 0; i < cell_count; i++) {
    init_cell(&cells[i], tb);
  }
  return cells;
}

/**
 * Allocate a link-id grid of rows*cols, zeroed (id 0 = no link).
 */
static uint16_t *alloc_link_ids(size_t rows, size_t cols) {
  size_t count;
  if (size_mul_overflow(rows, cols, &count)) {
    log_warn("Link id grid size overflow: %zux%zu", rows, cols);
    return nullptr;
  }
  return checked_calloc(count, sizeof(uint16_t));
}

/**
 * Allocate scrollback ring for capacity rows of width cols.
 * capacity == 0 returns nullptr without error (scrollback disabled).
 */
static TermCell *alloc_scrollback(size_t capacity, size_t cols) {
  size_t cell_count;

  if (capacity == 0) {
    return nullptr;
  }
  if (!mitr_scrollback_capacity_valid(capacity)) {
    log_warn("Scrollback capacity out of range: %zu", capacity);
    return nullptr;
  }
  if (size_mul_overflow(capacity, cols, &cell_count)) {
    log_warn("Scrollback size overflow: %zu x %zu", capacity, cols);
    return nullptr;
  }
  return checked_calloc(cell_count, sizeof(TermCell));
}

static uint16_t *alloc_scrollback_link_ids(size_t capacity, size_t cols) {
  size_t count;

  if (capacity == 0) {
    return nullptr;
  }
  if (!mitr_scrollback_capacity_valid(capacity)) {
    return nullptr;
  }
  if (size_mul_overflow(capacity, cols, &count)) {
    log_warn("Scrollback link id size overflow: %zu x %zu", capacity, cols);
    return nullptr;
  }
  return checked_calloc(count, sizeof(uint16_t));
}

/**
 * Copy the overlapping top-left region from an old grid into a new grid.
 */
static void copy_grid_top_left(TermCell *dst, size_t dst_rows, size_t dst_cols,
                               const TermCell *src, size_t src_rows,
                               size_t src_cols) {
  size_t copy_rows = (src_rows < dst_rows) ? src_rows : dst_rows;
  size_t copy_cols = (src_cols < dst_cols) ? src_cols : dst_cols;

  for (size_t row = 0; row < copy_rows; row++) {
    for (size_t col = 0; col < copy_cols; col++) {
      dst[row * dst_cols + col] = src[row * src_cols + col];
    }
  }
}

static void copy_link_ids_top_left(uint16_t *dst, size_t dst_rows,
                                   size_t dst_cols, const uint16_t *src,
                                   size_t src_rows, size_t src_cols) {
  size_t copy_rows;
  size_t copy_cols;

  if (!dst || !src) {
    return;
  }

  copy_rows = (src_rows < dst_rows) ? src_rows : dst_rows;
  copy_cols = (src_cols < dst_cols) ? src_cols : dst_cols;
  for (size_t row = 0; row < copy_rows; row++) {
    memcpy(&dst[row * dst_cols], &src[row * src_cols],
           copy_cols * sizeof(uint16_t));
  }
}

static void copy_row(TerminalBuffer *tb, size_t dst_row, size_t src_row) {
  memcpy(&tb->cells[dst_row * tb->cols], &tb->cells[src_row * tb->cols],
         tb->cols * sizeof(TermCell));
  if (tb->link_ids) {
    memcpy(&tb->link_ids[dst_row * tb->cols], &tb->link_ids[src_row * tb->cols],
           tb->cols * sizeof(uint16_t));
  }
}

static void move_rows(TerminalBuffer *tb, size_t dst_row, size_t src_row,
                      size_t n_rows) {
  memmove(&tb->cells[dst_row * tb->cols], &tb->cells[src_row * tb->cols],
          n_rows * tb->cols * sizeof(TermCell));
  if (tb->link_ids) {
    memmove(&tb->link_ids[dst_row * tb->cols],
            &tb->link_ids[src_row * tb->cols],
            n_rows * tb->cols * sizeof(uint16_t));
  }
}

static void move_cells_in_row(TerminalBuffer *tb, size_t row, size_t dst_col,
                              size_t src_col, size_t n_cols) {
  memmove(&tb->cells[row * tb->cols + dst_col],
          &tb->cells[row * tb->cols + src_col], n_cols * sizeof(TermCell));
  if (tb->link_ids) {
    memmove(&tb->link_ids[row * tb->cols + dst_col],
            &tb->link_ids[row * tb->cols + src_col], n_cols * sizeof(uint16_t));
  }
}

/**
 * Push one live-grid row into the scrollback ring, including link ids.
 */
static void scrollback_push_row(TerminalBuffer *tb, size_t src_row) {
  size_t write_idx;
  if (tb->scrollback_used < tb->scrollback_capacity) {
    write_idx =
        (tb->scrollback_start + tb->scrollback_used) % tb->scrollback_capacity;
    tb->scrollback_used++;
  } else {
    write_idx = tb->scrollback_start;
    tb->scrollback_start = (tb->scrollback_start + 1) % tb->scrollback_capacity;
  }
  memcpy(&tb->scrollback[write_idx * tb->cols], &tb->cells[src_row * tb->cols],
         tb->cols * sizeof(TermCell));
  if (tb->scrollback_link_ids && tb->link_ids) {
    memcpy(&tb->scrollback_link_ids[write_idx * tb->cols],
           &tb->link_ids[src_row * tb->cols], tb->cols * sizeof(uint16_t));
  }
}

static uint16_t intern_uri(TerminalBuffer *tb, const char *uri) {
  char truncated[HYPERLINK_URI_MAX + 1];
  size_t len = strlen(uri);

  if (len > HYPERLINK_URI_MAX) {
    len = HYPERLINK_URI_MAX;
  }
  memcpy(truncated, uri, len);
  truncated[len] = '\0';

  for (uint16_t i = 0; i < tb->link_uri_count; i++) {
    if (tb->link_uris[i] && strcmp(tb->link_uris[i], truncated) == 0) {
      return (uint16_t)(i + 1);
    }
  }

  if (tb->link_uri_count >= HYPERLINK_INTERN_MAX) {
    return 0;
  }

  char *copy = checked_malloc(len + 1);
  if (!copy) {
    return 0;
  }
  memcpy(copy, truncated, len + 1);
  tb->link_uris[tb->link_uri_count] = copy;
  tb->link_uri_count++;
  return tb->link_uri_count;
}

static void free_hyperlink_intern(TerminalBuffer *tb) {
  for (uint16_t i = 0; i < tb->link_uri_count; i++) {
    SAFE_FREE(tb->link_uris[i]);
  }
  tb->link_uri_count = 0;
  tb->current_link_id = 0;
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

TerminalBuffer *terminal_buffer_init(size_t rows, size_t cols) {
  log_trace("Initializing terminal buffer");

  if (!mitr_grid_dims_valid(rows, cols)) {
    fprintf(stderr, "ERROR: Invalid buffer size: %zux%zu (max %dx%d)\n", rows,
            cols, MITR_MAX_ROWS, MITR_MAX_COLS);
    return nullptr;
  }

  /* Allocate buffer structure */
  TerminalBuffer *tb = checked_calloc(1, sizeof(TerminalBuffer));
  if (!tb) {
    return nullptr;
  }

  /* Colors must be set before alloc_cell_grid uses them via init_cell */
  tb->default_fg = COLOR_DEFAULT_FG;
  tb->default_bg = COLOR_DEFAULT_BG;
  tb->current_attr = ATTR_NONE;
  tb->config_fg = COLOR_DEFAULT_FG;
  tb->config_bg = COLOR_DEFAULT_BG;

  tb->cells = alloc_cell_grid(tb, rows, cols);
  if (!tb->cells) {
    free(tb);
    return nullptr;
  }

  tb->link_ids = alloc_link_ids(rows, cols);
  if (!tb->link_ids) {
    SAFE_FREE(tb->cells);
    free(tb);
    return nullptr;
  }

  /* Initialize fields */
  tb->rows = rows;
  tb->cols = cols;
  tb->cursor_row = 0;
  tb->cursor_col = 0;
  tb->cursor_visible = true;
  tb->pending_wrap = false;

  /* VT100: Scrolling region defaults to full screen */
  tb->scroll_top = 0;
  tb->scroll_bottom = rows - 1;

  /* VT100: Default modes */
  tb->origin_mode = false; /* DECOM off - cursor absolute */
  tb->auto_wrap = true;    /* DECAWM on - auto-wrap enabled */

  /* VT220: Initialize tab stops (every 8 columns by default) */
  tb->tab_stops = (bool *)checked_calloc(cols, sizeof(bool));
  if (!tb->tab_stops) {
    SAFE_FREE(tb->cells);
    SAFE_FREE(tb->link_ids);
    free(tb);
    return nullptr;
  }
  init_default_tab_stops(tb->tab_stops, cols);

  tb->dirty_rows = checked_calloc(rows, sizeof(uint8_t));
  if (!tb->dirty_rows) {
    SAFE_FREE(tb->cells);
    SAFE_FREE(tb->link_ids);
    SAFE_FREE(tb->tab_stops);
    free(tb);
    return nullptr;
  }
  mark_all_visible_dirty(tb);

  /* VT320: Initialize display modes */
  tb->reverse_screen = false; /* Normal mode by default */

  /* XTERM: Initialize alternate screen buffer state */
  tb->alt_cells = nullptr; /* Allocated on demand */
  tb->using_alt_screen = false;
  tb->main_cursor_row = 0;
  tb->main_cursor_col = 0;
  tb->main_default_fg = tb->config_fg;
  tb->main_default_bg = tb->config_bg;
  tb->main_current_attr = ATTR_NONE;

  /* Scrollback buffer — failure is non-fatal at init */
  tb->scrollback_capacity = SCROLLBACK_DEFAULT_CAPACITY;
  tb->scrollback_used = 0;
  tb->scrollback_start = 0;
  tb->scroll_offset = 0;
  tb->scrollback = alloc_scrollback(tb->scrollback_capacity, cols);
  if (tb->scrollback) {
    tb->scrollback_link_ids =
        alloc_scrollback_link_ids(tb->scrollback_capacity, cols);
    if (!tb->scrollback_link_ids) {
      SAFE_FREE(tb->scrollback);
    }
  }

  log_debug("Terminal buffer initialized: %zux%zu", rows, cols);

  return tb;
}

void terminal_buffer_destroy(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }

  log_trace("Destroying terminal buffer");

  free_hyperlink_intern(tb);
  SAFE_FREE(tb->cells);
  SAFE_FREE(tb->link_ids);
  SAFE_FREE(tb->alt_cells);
  SAFE_FREE(tb->alt_link_ids);
  SAFE_FREE(tb->tab_stops);
  SAFE_FREE(tb->dirty_rows);
  SAFE_FREE(tb->scrollback);
  SAFE_FREE(tb->scrollback_link_ids);
  free(tb);
}

void terminal_buffer_set_colors(TerminalBuffer *tb, uint32_t fg_color,
                                uint32_t bg_color) {
  if (!tb)
    return;

  tb->default_fg = fg_color;
  tb->default_bg = bg_color;
  tb->config_fg = fg_color;
  tb->config_bg = bg_color;

  log_debug("Terminal buffer colors set: fg=0x%06x bg=0x%06x", fg_color,
            bg_color);
}

void terminal_buffer_clear(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }

  log_trace("Clearing terminal buffer");

  /* Reset all cells */
  for (size_t row = 0; row < tb->rows; row++) {
    for (size_t col = 0; col < tb->cols; col++) {
      init_cell_at(tb, row, col);
    }
  }

  /* Reset cursor */
  tb->cursor_row = 0;
  tb->cursor_col = 0;
  tb->pending_wrap = false;
  mark_all_visible_dirty(tb);
}

TermCell *terminal_buffer_get_cell(TerminalBuffer *tb, size_t row, size_t col) {
  if (!tb) {
    return nullptr;
  }

  /* Bounds check */
  if (row >= tb->rows || col >= tb->cols) {
    return nullptr;
  }

  size_t index = get_index(tb, row, col);
  return &tb->cells[index];
}

const TermCell *terminal_buffer_get_cell_const(const TerminalBuffer *tb,
                                               size_t row, size_t col) {
  if (!tb || row >= tb->rows || col >= tb->cols) {
    return nullptr;
  }
  return &tb->cells[row * tb->cols + col];
}

size_t terminal_buffer_snap_column(const TerminalBuffer *tb, size_t row,
                                   size_t col) {
  if (!tb || row >= tb->rows || col >= tb->cols) {
    return col;
  }
  const TermCell *row_cells = &tb->cells[row * tb->cols];
  while (col > 0 && (row_cells[col].flags & CELL_FLAG_CONTINUATION) != 0) {
    col--;
  }
  return col;
}

void terminal_buffer_set_cursor(TerminalBuffer *tb, size_t row, size_t col) {
  if (!tb) {
    return;
  }

  tb->pending_wrap = false;

  /* VT100: DECOM (Origin Mode) - cursor position relative to scroll region */
  if (tb->origin_mode) {
    /* In origin mode, row is relative to scroll_top */
    row = tb->scroll_top + row;

    /* Clamp to scrolling region */
    if (row < tb->scroll_top) {
      row = tb->scroll_top;
    }
    if (row > tb->scroll_bottom) {
      row = tb->scroll_bottom;
    }
  } else {
    /* Normal mode - absolute positioning */
    if (row >= tb->rows) {
      row = tb->rows - 1;
    }
  }

  /* Column is always absolute */
  if (col >= tb->cols) {
    col = tb->cols - 1;
  }

  col = terminal_buffer_snap_column(tb, row, col);

  mark_live_row_dirty(tb, tb->cursor_row);
  tb->cursor_row = row;
  tb->cursor_col = col;
  mark_live_row_dirty(tb, tb->cursor_row);
}

void terminal_buffer_get_cursor(TerminalBuffer *tb, size_t *row, size_t *col) {
  if (!tb) {
    if (row)
      *row = 0;
    if (col)
      *col = 0;
    return;
  }

  if (row) {
    *row = tb->cursor_row;
  }
  if (col) {
    *col = tb->cursor_col;
  }
}

void terminal_buffer_put_char(TerminalBuffer *tb, char ch) {
  /* Delegate to put_codepoint */
  terminal_buffer_put_codepoint(tb, (uint32_t)(unsigned char)ch);
}

void terminal_buffer_put_codepoint(TerminalBuffer *tb, uint32_t codepoint) {
  if (!tb) {
    return;
  }

  /* If pending wrap is active, execute the deferred wrap now */
  if (tb->pending_wrap) {
    tb->pending_wrap = false;
    if (tb->auto_wrap) {
      tb->cursor_col = 0;
      terminal_buffer_index(tb);
    }
  }

  int width = unicode_wcwidth(codepoint);
  if (width < 0) {
    width = 1;
  }

  if (width == 0) {
    TermCell *cell =
        terminal_buffer_get_cell(tb, tb->cursor_row, tb->cursor_col);
    if (!cell) {
      return;
    }
    cell->codepoint = codepoint;
    cell->width = 0;
    cell->flags = 0;
    cell->fg_color = tb->default_fg;
    cell->bg_color = tb->default_bg;
    cell->attributes = tb->current_attr;
    set_cell_link_id(tb, tb->cursor_row, tb->cursor_col, tb->current_link_id);
    mark_live_row_dirty(tb, tb->cursor_row);
    return;
  }

  if (width == 2 && tb->cursor_col + 1 >= tb->cols) {
    if (tb->auto_wrap && tb->cursor_col == tb->cols - 1) {
      tb->cursor_col = 0;
      terminal_buffer_index(tb);
    } else if (tb->cursor_col + 1 >= tb->cols) {
      width = 1;
    }
  }

  if (tb->insert_mode) {
    terminal_buffer_insert_chars(tb, (size_t)width);
  }

  TermCell *cell = terminal_buffer_get_cell(tb, tb->cursor_row, tb->cursor_col);
  if (!cell) {
    return;
  }

  cell->codepoint = codepoint;
  cell->width = (uint8_t)width;
  cell->flags = 0;
  cell->fg_color = tb->default_fg;
  cell->bg_color = tb->default_bg;
  cell->attributes = tb->current_attr;

  set_cell_link_id(tb, tb->cursor_row, tb->cursor_col, tb->current_link_id);

  if (width == 2) {
    TermCell *cont =
        terminal_buffer_get_cell(tb, tb->cursor_row, tb->cursor_col + 1);
    if (cont) {
      cont->codepoint = 0;
      cont->width = 0;
      cont->flags = CELL_FLAG_CONTINUATION;
      cont->fg_color = cell->fg_color;
      cont->bg_color = cell->bg_color;
      cont->attributes = cell->attributes;
      set_cell_link_id(tb, tb->cursor_row, tb->cursor_col + 1,
                       tb->current_link_id);
    }
  }

  tb->cursor_col += (size_t)width;

  if (tb->cursor_col >= tb->cols) {
    if (tb->auto_wrap) {
      tb->cursor_col = tb->cols - 1;
      tb->pending_wrap = true;
    } else {
      tb->cursor_col = tb->cols - 1;
    }
  }

  mark_live_row_dirty(tb, tb->cursor_row);
}

void terminal_buffer_scroll_up(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }

  tb->pending_wrap = false;

  /* Save top row to scrollback before it's lost (main screen, full-width
   * scroll region only — sub-region scrolls are application-internal). */
  if (!tb->using_alt_screen && tb->scroll_top == 0 && tb->scrollback) {
    scrollback_push_row(tb, 0);
  }
  tb->scroll_offset = 0;

  log_trace("Scrolling buffer up (region: %zu-%zu)", tb->scroll_top,
            tb->scroll_bottom);

  /* Scroll only within the scrolling region */
  size_t top = tb->scroll_top;
  size_t bottom = tb->scroll_bottom;

  /* Move rows up by one within the scrolling region */
  for (size_t row = top; row < bottom; row++) {
    copy_row(tb, row, row + 1);
  }

  /* Clear bottom row of scrolling region */
  for (size_t col = 0; col < tb->cols; col++) {
    init_cell_at(tb, bottom, col);
  }

  record_scroll_shift(tb, top, bottom, 1);
}

void terminal_buffer_set_scroll_region(TerminalBuffer *tb, size_t top,
                                       size_t bottom) {
  if (!tb) {
    return;
  }

  tb->pending_wrap = false;

  /* Clamp to valid range */
  if (top >= tb->rows) {
    top = 0;
  }
  if (bottom >= tb->rows) {
    bottom = tb->rows - 1;
  }
  if (top > bottom) {
    /* Invalid region, reset to full screen */
    top = 0;
    bottom = tb->rows - 1;
  }

  tb->scroll_top = top;
  tb->scroll_bottom = bottom;

  log_debug("Set scroll region: %zu-%zu", top, bottom);
}

void terminal_buffer_reset_scroll_region(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }

  tb->pending_wrap = false;
  tb->scroll_top = 0;
  tb->scroll_bottom = tb->rows - 1;

  log_debug("Reset scroll region to full screen");
}

void terminal_buffer_insert_lines(TerminalBuffer *tb, size_t count) {
  if (!tb || count == 0) {
    return;
  }

  tb->pending_wrap = false;
  size_t cursor_row = tb->cursor_row;

  /* Only works within scrolling region */
  if (cursor_row < tb->scroll_top || cursor_row > tb->scroll_bottom) {
    return;
  }

  log_debug("Insert %zu lines at row %zu (scroll region: %zu-%zu)", count,
            cursor_row, tb->scroll_top, tb->scroll_bottom);

  /* Limit count to available space */
  size_t available = tb->scroll_bottom - cursor_row + 1;
  if (count > available) {
    count = available;
  }

  /* Move lines down by count in one pass */
  size_t lines_to_move = available - count;
  if (lines_to_move > 0) {
    move_rows(tb, cursor_row + count, cursor_row, lines_to_move);
  }

  /* Clear the inserted lines */
  for (size_t row = cursor_row; row < cursor_row + count; row++) {
    for (size_t col = 0; col < tb->cols; col++) {
      init_cell_at(tb, row, col);
    }
  }

  record_scroll_shift(tb, cursor_row, tb->scroll_bottom, -(int)count);
}

void terminal_buffer_delete_lines(TerminalBuffer *tb, size_t count) {
  if (!tb || count == 0) {
    return;
  }

  tb->pending_wrap = false;
  size_t cursor_row = tb->cursor_row;

  /* Only works within scrolling region */
  if (cursor_row < tb->scroll_top || cursor_row > tb->scroll_bottom) {
    return;
  }

  log_debug("Delete %zu lines at row %zu (scroll region: %zu-%zu)", count,
            cursor_row, tb->scroll_top, tb->scroll_bottom);

  /* Limit count to available space */
  size_t available = tb->scroll_bottom - cursor_row + 1;
  if (count > available) {
    count = available;
  }

  /* Move lines up by count in one pass */
  size_t lines_to_move = available - count;
  if (lines_to_move > 0) {
    move_rows(tb, cursor_row, cursor_row + count, lines_to_move);
  }

  /* Clear the vacated lines at the bottom */
  for (size_t row = tb->scroll_bottom - count + 1; row <= tb->scroll_bottom;
       row++) {
    for (size_t col = 0; col < tb->cols; col++) {
      init_cell_at(tb, row, col);
    }
  }

  record_scroll_shift(tb, cursor_row, tb->scroll_bottom, (int)count);
}

/* ========================================================================
 * VT100 Functions
 * ======================================================================== */

void terminal_buffer_scroll_down(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }

  tb->pending_wrap = false;

  log_trace("Scrolling buffer down (region: %zu-%zu)", tb->scroll_top,
            tb->scroll_bottom);

  /* Scroll down within the scrolling region (opposite of scroll_up) */
  size_t top = tb->scroll_top;
  size_t bottom = tb->scroll_bottom;

  /* Move rows down by one within the scrolling region */
  for (size_t row = bottom; row > top; row--) {
    copy_row(tb, row, row - 1);
  }

  /* Clear top row of scrolling region */
  for (size_t col = 0; col < tb->cols; col++) {
    init_cell_at(tb, top, col);
  }

  record_scroll_shift(tb, top, bottom, -1);
}

void terminal_buffer_index(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }

  /* VT100: IND - Move cursor down one line, scroll if at bottom of scroll
   * region */
  if (tb->cursor_row == tb->scroll_bottom) {
    /* At bottom of scroll region - scroll up */
    terminal_buffer_scroll_up(tb);
  } else {
    /* Not at bottom - just move cursor down */
    mark_live_row_dirty(tb, tb->cursor_row);
    tb->cursor_row++;
    if (tb->cursor_row >= tb->rows) {
      tb->cursor_row = tb->rows - 1;
    }
    mark_live_row_dirty(tb, tb->cursor_row);
  }
}

void terminal_buffer_reverse_index(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }

  /* VT100: RI - Move cursor up one line, scroll down if at top of scroll
   * region */
  if (tb->cursor_row == tb->scroll_top) {
    /* At top of scroll region - scroll down */
    terminal_buffer_scroll_down(tb);
  } else {
    /* Not at top - just move cursor up */
    mark_live_row_dirty(tb, tb->cursor_row);
    if (tb->cursor_row > 0) {
      tb->cursor_row--;
    }
    mark_live_row_dirty(tb, tb->cursor_row);
  }
}

/* ========================================================================
 * VT102: Character Insert/Delete Functions
 * ======================================================================== */

/**
 * VT102: Insert characters at cursor position - ICH
 * Characters to the right are shifted right, characters pushed past end are
 * lost
 */
void terminal_buffer_insert_chars(TerminalBuffer *tb, size_t count) {
  if (!tb || count == 0)
    return;

  tb->pending_wrap = false;
  size_t row = tb->cursor_row;
  size_t col = tb->cursor_col;

  /* Clamp count to not exceed line width */
  if (col + count > tb->cols) {
    count = tb->cols - col;
  }

  log_debug("VT102: ICH - Inserting %zu characters at (%zu,%zu)", count, row,
            col);

  /* Shift characters to the right */
  size_t chars_to_move = tb->cols - col - count;
  if (chars_to_move > 0) {
    move_cells_in_row(tb, row, col + count, col, chars_to_move);
  }

  /* Fill inserted positions with spaces */
  for (size_t i = 0; i < count; i++) {
    init_cell_at(tb, row, col + i);
  }

  mark_live_row_dirty(tb, row);
}

/**
 * VT102: Delete characters at cursor position - DCH
 * Characters to the right are shifted left, spaces are added at end
 */
void terminal_buffer_delete_chars(TerminalBuffer *tb, size_t count) {
  if (!tb || count == 0)
    return;

  tb->pending_wrap = false;
  size_t row = tb->cursor_row;
  size_t col = tb->cursor_col;

  /* Clamp count to not exceed remaining line width */
  if (col + count > tb->cols) {
    count = tb->cols - col;
  }

  log_debug("VT102: DCH - Deleting %zu characters at (%zu,%zu)", count, row,
            col);

  /* Shift characters to the left */
  size_t chars_to_move = tb->cols - col - count;
  if (chars_to_move > 0) {
    move_cells_in_row(tb, row, col, col + count, chars_to_move);
  }

  /* Fill end positions with spaces */
  for (size_t i = tb->cols - count; i < tb->cols; i++) {
    init_cell_at(tb, row, i);
  }

  mark_live_row_dirty(tb, row);
}

/**
 * VT102: Erase characters at cursor position - ECH
 * Replace characters with spaces, cursor does not move
 */
void terminal_buffer_erase_chars(TerminalBuffer *tb, size_t count) {
  if (!tb || count == 0)
    return;

  tb->pending_wrap = false;
  size_t row = tb->cursor_row;
  size_t col = tb->cursor_col;

  /* Clamp count to not exceed line width */
  if (col + count > tb->cols) {
    count = tb->cols - col;
  }

  log_debug("VT102: ECH - Erasing %zu characters at (%zu,%zu)", count, row,
            col);

  /* Erase characters (replace with spaces) */
  for (size_t i = 0; i < count; i++) {
    init_cell_at(tb, row, col + i);
  }

  mark_live_row_dirty(tb, row);
}

/* ========================================================================
 * VT220: Scrolling Functions
 * ======================================================================== */

/**
 * VT220: Scroll up n lines within scrolling region - SU
 */
void terminal_buffer_scroll_up_n(TerminalBuffer *tb, size_t count) {
  if (!tb || count == 0)
    return;

  tb->pending_wrap = false;
  size_t top = tb->scroll_top;
  size_t bottom = tb->scroll_bottom;
  size_t region_size = bottom - top + 1;

  if (!tb->using_alt_screen && top == 0 && tb->scrollback) {
    size_t save_count = (count < region_size) ? count : region_size;
    for (size_t i = 0; i < save_count; i++) {
      scrollback_push_row(tb, i);
    }
  }
  tb->scroll_offset = 0;

  if (count >= region_size) {
    /* Clear entire region */
    for (size_t row = top; row <= bottom; row++) {
      for (size_t col = 0; col < tb->cols; col++) {
        init_cell_at(tb, row, col);
      }
    }
    mark_all_visible_dirty(tb);
    return;
  }

  /* Move rows up by count */
  move_rows(tb, top, top + count, region_size - count);

  /* Clear vacated rows at bottom */
  for (size_t row = bottom - count + 1; row <= bottom; row++) {
    for (size_t col = 0; col < tb->cols; col++) {
      init_cell_at(tb, row, col);
    }
  }

  record_scroll_shift(tb, top, bottom, (int)count);
}

/**
 * VT220: Scroll down n lines within scrolling region - SD
 */
void terminal_buffer_scroll_down_n(TerminalBuffer *tb, size_t count) {
  if (!tb || count == 0)
    return;

  tb->pending_wrap = false;
  size_t top = tb->scroll_top;
  size_t bottom = tb->scroll_bottom;
  size_t region_size = bottom - top + 1;

  if (count >= region_size) {
    /* Clear entire region */
    for (size_t row = top; row <= bottom; row++) {
      for (size_t col = 0; col < tb->cols; col++) {
        init_cell_at(tb, row, col);
      }
    }
    mark_all_visible_dirty(tb);
    return;
  }

  /* Move rows down by count */
  move_rows(tb, top + count, top, region_size - count);

  /* Clear vacated rows at top */
  for (size_t row = top; row < top + count; row++) {
    for (size_t col = 0; col < tb->cols; col++) {
      init_cell_at(tb, row, col);
    }
  }

  record_scroll_shift(tb, top, bottom, -(int)count);
}

/* ========================================================================
 * VT220: Tab Stop Management
 * ======================================================================== */

/**
 * VT220: Set tab stop at current cursor column - HTS
 */
void terminal_buffer_set_tab_stop(TerminalBuffer *tb) {
  if (!tb || !tb->tab_stops) {
    log_debug("VT220: HTS - ERROR: tb or tab_stops is nullptr");
    return;
  }

  size_t col = tb->cursor_col;
  if (col < tb->cols) {
    tb->tab_stops[col] = true;
    log_debug("VT220: HTS - Set tab stop at column %zu", col);
  } else {
    log_debug("VT220: HTS - ERROR: col %zu >= cols %zu", col, tb->cols);
  }
}

/**
 * VT220: Clear tab stop at current cursor column - TBC mode 0
 */
void terminal_buffer_clear_tab_stop(TerminalBuffer *tb) {
  if (!tb || !tb->tab_stops)
    return;

  size_t col = tb->cursor_col;
  if (col < tb->cols) {
    tb->tab_stops[col] = false;
    log_debug("VT220: TBC - Cleared tab stop at column %zu", col);
  }
}

/**
 * VT220: Clear all tab stops - TBC mode 3
 */
void terminal_buffer_clear_all_tab_stops(TerminalBuffer *tb) {
  if (!tb || !tb->tab_stops)
    return;

  for (size_t i = 0; i < tb->cols; i++) {
    tb->tab_stops[i] = false;
  }

  log_debug("VT220: TBC - Cleared all tab stops");
}

/**
 * VT220: Get next tab stop position from current column
 */
size_t terminal_buffer_get_next_tab_stop(TerminalBuffer *tb) {
  if (!tb) {
    return 0;
  }

  if (!tb->tab_stops) {
    /* Fallback to 8-column tab stops */
    size_t col = tb->cursor_col;
    size_t next_tab = ((col / 8) + 1) * 8;
    if (next_tab >= tb->cols) {
      next_tab = tb->cols - 1;
    }
    log_debug("VT220: get_next_tab_stop (no tab_stops) from %zu -> %zu", col,
              next_tab);
    return next_tab;
  }

  size_t col = tb->cursor_col;

  /* Find next tab stop after current position */
  for (size_t i = col + 1; i < tb->cols; i++) {
    if (tb->tab_stops[i]) {
      log_debug("VT220: get_next_tab_stop from %zu -> %zu", col, i);
      return i;
    }
  }

  /* No more tab stops, go to end of line (standard VT behavior) */
  log_debug("VT220: get_next_tab_stop from %zu -> %zu (no more stops)", col,
            tb->cols - 1);
  return tb->cols - 1;
}

/**
 * VT220: Get previous tab stop position from current column
 */
size_t terminal_buffer_get_prev_tab_stop(TerminalBuffer *tb) {
  if (!tb) {
    return 0;
  }

  size_t col = tb->cursor_col;
  if (col == 0) {
    return 0;
  }

  if (!tb->tab_stops) {
    size_t prev_tab = (col / 8) * 8;
    if (prev_tab == col) {
      prev_tab -= 8;
    }
    return prev_tab;
  }

  size_t i = col;
  while (i > 0) {
    i--;
    if (tb->tab_stops[i]) {
      return i;
    }
  }
  return 0;
}

/* ========================================================================
 * VT220: Soft Reset
 * ======================================================================== */

/**
 * VT220: Soft reset - reset terminal to initial state
 * Preserves screen content, resets attributes, colors, modes
 */
void terminal_buffer_soft_reset(TerminalBuffer *tb) {
  if (!tb)
    return;

  log_debug("VT220: Soft reset");

  tb->pending_wrap = false;

  /* Reset attributes */
  tb->current_attr = ATTR_NONE;

  /* Reset colors to configured defaults */
  tb->default_fg = tb->config_fg;
  tb->default_bg = tb->config_bg;

  /* Reset scrolling region to full screen */
  tb->scroll_top = 0;
  tb->scroll_bottom = tb->rows - 1;

  /* Reset modes */
  tb->origin_mode = false;
  tb->auto_wrap = true;
  tb->insert_mode = false;
  tb->cursor_visible = true;

  /* Reset cursor position */
  mark_live_row_dirty(tb, tb->cursor_row);
  tb->cursor_row = 0;
  tb->cursor_col = 0;
  mark_live_row_dirty(tb, tb->cursor_row);

  /* Reset saved cursor state */
  tb->cursor_saved_row = 0;
  tb->cursor_saved_col = 0;
  tb->cursor_saved_attr = ATTR_NONE;
  tb->cursor_saved_fg = tb->config_fg;
  tb->cursor_saved_bg = tb->config_bg;

  /* Reset tab stops to default (every 8 columns) */
  if (tb->tab_stops) {
    for (size_t i = 0; i < tb->cols; i++) {
      tb->tab_stops[i] = (i % 8 == 0 && i > 0);
    }
  }
}

/* ========================================================================
 * VT320: Column Mode Switching (DECCOLM)
 * ======================================================================== */

/**
 * VT320: Resize terminal buffer columns - DECCOLM
 * Used for switching between 80 and 132 column modes.
 * All replacements are allocated before any commit (transactional).
 */
MitrError terminal_buffer_resize_columns(TerminalBuffer *tb, size_t new_cols) {
  if (!tb || !mitr_grid_dims_valid(tb->rows, new_cols)) {
    return MITR_ERROR_INVALID_PARAM;
  }

  /* If already at target size, do nothing */
  if (tb->cols == new_cols) {
    return MITR_OK;
  }

  log_debug("VT320: DECCOLM - Resizing from %zu to %zu columns", tb->cols,
            new_cols);

  /* Stage all new allocations; leave tb unchanged until everything succeeds. */
  TermCell *new_cells = alloc_cell_grid(tb, tb->rows, new_cols);
  if (!new_cells) {
    return MITR_ERROR_MEMORY;
  }

  uint16_t *new_link_ids = alloc_link_ids(tb->rows, new_cols);
  if (!new_link_ids) {
    free(new_cells);
    return MITR_ERROR_MEMORY;
  }

  bool *new_tab_stops = (bool *)checked_calloc(new_cols, sizeof(bool));
  if (!new_tab_stops) {
    free(new_cells);
    free(new_link_ids);
    return MITR_ERROR_MEMORY;
  }
  init_default_tab_stops(new_tab_stops, new_cols);

  TermCell *new_alt_cells = nullptr;
  uint16_t *new_alt_link_ids = nullptr;
  if (tb->alt_cells) {
    new_alt_cells = alloc_cell_grid(tb, tb->rows, new_cols);
    if (!new_alt_cells) {
      free(new_cells);
      free(new_link_ids);
      free(new_tab_stops);
      return MITR_ERROR_MEMORY;
    }
    new_alt_link_ids = alloc_link_ids(tb->rows, new_cols);
    if (!new_alt_link_ids) {
      free(new_cells);
      free(new_link_ids);
      free(new_tab_stops);
      free(new_alt_cells);
      return MITR_ERROR_MEMORY;
    }
  }

  TermCell *new_scrollback = nullptr;
  uint16_t *new_scrollback_link_ids = nullptr;
  if (tb->scrollback_capacity > 0) {
    new_scrollback = alloc_scrollback(tb->scrollback_capacity, new_cols);
    if (!new_scrollback) {
      free(new_cells);
      free(new_link_ids);
      free(new_tab_stops);
      free(new_alt_cells);
      free(new_alt_link_ids);
      return MITR_ERROR_MEMORY;
    }
    new_scrollback_link_ids =
        alloc_scrollback_link_ids(tb->scrollback_capacity, new_cols);
    if (!new_scrollback_link_ids) {
      free(new_cells);
      free(new_link_ids);
      free(new_tab_stops);
      free(new_alt_cells);
      free(new_alt_link_ids);
      free(new_scrollback);
      return MITR_ERROR_MEMORY;
    }
  }

  /* Commit */
  SAFE_FREE(tb->cells);
  SAFE_FREE(tb->link_ids);
  SAFE_FREE(tb->tab_stops);
  SAFE_FREE(tb->alt_cells);
  SAFE_FREE(tb->alt_link_ids);
  SAFE_FREE(tb->scrollback);
  SAFE_FREE(tb->scrollback_link_ids);

  tb->cells = new_cells;
  tb->link_ids = new_link_ids;
  tb->tab_stops = new_tab_stops;
  tb->alt_cells = new_alt_cells;
  tb->alt_link_ids = new_alt_link_ids;
  tb->scrollback = new_scrollback;
  tb->scrollback_link_ids = new_scrollback_link_ids;
  tb->cols = new_cols;

  /* VT320: DECCOLM clears screen and resets cursor */
  tb->cursor_row = 0;
  tb->cursor_col = 0;
  tb->pending_wrap = false;

  /* Reset scrolling region to full screen */
  tb->scroll_top = 0;
  tb->scroll_bottom = tb->rows - 1;

  tb->scrollback_used = 0;
  tb->scrollback_start = 0;
  tb->scroll_offset = 0;
  mark_all_visible_dirty(tb);

  log_debug("VT320: DECCOLM - Resize complete, cursor reset to (0,0)");

  return MITR_OK;
}

/**
 * Resize terminal buffer to new dimensions.
 * Preserves content where possible (top-left aligned).
 * All replacements are allocated before any commit (transactional).
 */
MitrError terminal_buffer_resize(TerminalBuffer *tb, size_t new_rows,
                                 size_t new_cols) {
  if (!tb || !mitr_grid_dims_valid(new_rows, new_cols)) {
    return MITR_ERROR_INVALID_PARAM;
  }

  /* If already at target size, do nothing */
  if (tb->rows == new_rows && tb->cols == new_cols) {
    return MITR_OK;
  }

  log_debug("Resizing terminal buffer from %zux%zu to %zux%zu", tb->rows,
            tb->cols, new_rows, new_cols);

  size_t old_rows = tb->rows;
  size_t old_cols = tb->cols;

  /* Stage all new allocations; leave tb unchanged until everything succeeds. */
  TermCell *new_cells = alloc_cell_grid(tb, new_rows, new_cols);
  if (!new_cells) {
    return MITR_ERROR_MEMORY;
  }

  uint16_t *new_link_ids = alloc_link_ids(new_rows, new_cols);
  if (!new_link_ids) {
    free(new_cells);
    return MITR_ERROR_MEMORY;
  }

  bool *new_tab_stops = (bool *)checked_calloc(new_cols, sizeof(bool));
  if (!new_tab_stops) {
    free(new_cells);
    free(new_link_ids);
    return MITR_ERROR_MEMORY;
  }
  init_default_tab_stops(new_tab_stops, new_cols);

  TermCell *new_alt_cells = nullptr;
  uint16_t *new_alt_link_ids = nullptr;
  if (tb->alt_cells) {
    new_alt_cells = alloc_cell_grid(tb, new_rows, new_cols);
    if (!new_alt_cells) {
      free(new_cells);
      free(new_link_ids);
      free(new_tab_stops);
      return MITR_ERROR_MEMORY;
    }
    new_alt_link_ids = alloc_link_ids(new_rows, new_cols);
    if (!new_alt_link_ids) {
      free(new_cells);
      free(new_link_ids);
      free(new_tab_stops);
      free(new_alt_cells);
      return MITR_ERROR_MEMORY;
    }
  }

  TermCell *new_scrollback = nullptr;
  uint16_t *new_scrollback_link_ids = nullptr;
  bool replace_scrollback = (new_cols != old_cols);
  if (replace_scrollback && tb->scrollback_capacity > 0) {
    new_scrollback = alloc_scrollback(tb->scrollback_capacity, new_cols);
    if (!new_scrollback) {
      free(new_cells);
      free(new_link_ids);
      free(new_tab_stops);
      free(new_alt_cells);
      free(new_alt_link_ids);
      return MITR_ERROR_MEMORY;
    }
    new_scrollback_link_ids =
        alloc_scrollback_link_ids(tb->scrollback_capacity, new_cols);
    if (!new_scrollback_link_ids) {
      free(new_cells);
      free(new_link_ids);
      free(new_tab_stops);
      free(new_alt_cells);
      free(new_alt_link_ids);
      free(new_scrollback);
      return MITR_ERROR_MEMORY;
    }
  }

  copy_grid_top_left(new_cells, new_rows, new_cols, tb->cells, old_rows,
                     old_cols);
  copy_link_ids_top_left(new_link_ids, new_rows, new_cols, tb->link_ids,
                         old_rows, old_cols);
  if (new_alt_cells) {
    copy_grid_top_left(new_alt_cells, new_rows, new_cols, tb->alt_cells,
                       old_rows, old_cols);
    copy_link_ids_top_left(new_alt_link_ids, new_rows, new_cols,
                           tb->alt_link_ids, old_rows, old_cols);
  }

  uint8_t *new_dirty = nullptr;
  if (new_rows != old_rows) {
    new_dirty = checked_calloc(new_rows, sizeof(uint8_t));
    if (!new_dirty) {
      free(new_cells);
      free(new_link_ids);
      free(new_tab_stops);
      free(new_alt_cells);
      free(new_alt_link_ids);
      free(new_scrollback);
      free(new_scrollback_link_ids);
      return MITR_ERROR_MEMORY;
    }
  }

  /* Commit */
  SAFE_FREE(tb->cells);
  SAFE_FREE(tb->link_ids);
  SAFE_FREE(tb->tab_stops);
  SAFE_FREE(tb->alt_cells);
  SAFE_FREE(tb->alt_link_ids);
  tb->cells = new_cells;
  tb->link_ids = new_link_ids;
  tb->tab_stops = new_tab_stops;
  tb->alt_cells = new_alt_cells;
  tb->alt_link_ids = new_alt_link_ids;
  tb->rows = new_rows;
  tb->cols = new_cols;

  if (replace_scrollback) {
    SAFE_FREE(tb->scrollback);
    SAFE_FREE(tb->scrollback_link_ids);
    tb->scrollback = new_scrollback;
    tb->scrollback_link_ids = new_scrollback_link_ids;
    tb->scrollback_used = 0;
    tb->scrollback_start = 0;
  }

  tb->pending_wrap = false;
  tb->scroll_offset = 0;

  /* Clamp cursor to new dimensions */
  if (tb->cursor_row >= new_rows) {
    tb->cursor_row = new_rows - 1;
  }
  if (tb->cursor_col >= new_cols) {
    tb->cursor_col = new_cols - 1;
  }
  if (tb->main_cursor_row >= new_rows) {
    tb->main_cursor_row = new_rows - 1;
  }
  if (tb->main_cursor_col >= new_cols) {
    tb->main_cursor_col = new_cols - 1;
  }

  /* Update scrolling region */
  tb->scroll_top = 0;
  tb->scroll_bottom = new_rows - 1;

  if (new_dirty) {
    SAFE_FREE(tb->dirty_rows);
    tb->dirty_rows = new_dirty;
  }
  mark_all_visible_dirty(tb);

  log_debug("Terminal buffer resize complete: %zux%zu, cursor at (%zu,%zu)",
            new_rows, new_cols, tb->cursor_row, tb->cursor_col);

  return MITR_OK;
}

/* ========================================================================
 * XTERM: Alternate Screen Buffer
 * ======================================================================== */

/**
 * XTERM: Enter alternate screen buffer
 * Saves current screen content and cursor, switches to alternate buffer
 */
MitrError terminal_buffer_enter_alt_screen(TerminalBuffer *tb) {
  if (!tb) {
    return MITR_ERROR_INVALID_PARAM;
  }

  /* Already in alternate screen? */
  if (tb->using_alt_screen) {
    log_debug("XTERM: Already in alternate screen");
    return MITR_OK;
  }

  tb->pending_wrap = false;

  /* Allocate alternate buffer if not already allocated */
  if (!tb->alt_cells) {
    tb->alt_cells = alloc_cell_grid(tb, tb->rows, tb->cols);
    if (!tb->alt_cells) {
      log_warn("XTERM: Failed to allocate alternate screen buffer");
      return MITR_ERROR_MEMORY;
    }
    tb->alt_link_ids = alloc_link_ids(tb->rows, tb->cols);
    if (!tb->alt_link_ids) {
      SAFE_FREE(tb->alt_cells);
      log_warn("XTERM: Failed to allocate alternate screen link ids");
      return MITR_ERROR_MEMORY;
    }
  }

  /* Save main screen cursor and attributes */
  tb->main_cursor_row = tb->cursor_row;
  tb->main_cursor_col = tb->cursor_col;
  tb->main_default_fg = tb->default_fg;
  tb->main_default_bg = tb->default_bg;
  tb->main_current_attr = tb->current_attr;

  /* Swap buffers: main -> alt_cells, alt_cells -> cells (for drawing) */
  TermCell *temp = tb->alt_cells;
  tb->alt_cells = tb->cells; /* Save main screen content */
  tb->cells = temp;          /* Use alternate buffer for display */

  uint16_t *temp_ids = tb->alt_link_ids;
  tb->alt_link_ids = tb->link_ids;
  tb->link_ids = temp_ids;

  /* Clear alternate screen */
  for (size_t row = 0; row < tb->rows; row++) {
    for (size_t col = 0; col < tb->cols; col++) {
      init_cell_at(tb, row, col);
    }
  }

  /* Reset cursor to home position */
  tb->cursor_row = 0;
  tb->cursor_col = 0;

  /* Reset scroll region */
  tb->scroll_top = 0;
  tb->scroll_bottom = tb->rows - 1;

  tb->using_alt_screen = true;
  mark_all_visible_dirty(tb);

  log_debug("XTERM: Entered alternate screen buffer");

  return MITR_OK;
}

/**
 * XTERM: Exit alternate screen buffer
 * Restores saved screen content and cursor from main buffer
 */
void terminal_buffer_exit_alt_screen(TerminalBuffer *tb) {
  if (!tb) {
    return;
  }

  /* Not in alternate screen? */
  if (!tb->using_alt_screen) {
    log_debug("XTERM: Not in alternate screen, nothing to restore");
    return;
  }

  tb->pending_wrap = false;

  /* Swap buffers back: alt_cells (saved main) -> cells, cells (alt) ->
   * alt_cells */
  TermCell *temp = tb->cells;
  tb->cells = tb->alt_cells; /* Restore main screen content */
  tb->alt_cells = temp;      /* Keep alt buffer for potential reuse */

  uint16_t *temp_ids = tb->link_ids;
  tb->link_ids = tb->alt_link_ids;
  tb->alt_link_ids = temp_ids;

  /* Restore cursor and attributes */
  tb->cursor_row = tb->main_cursor_row;
  tb->cursor_col = tb->main_cursor_col;
  tb->default_fg = tb->main_default_fg;
  tb->default_bg = tb->main_default_bg;
  tb->current_attr = tb->main_current_attr;

  /* Reset scroll region */
  tb->scroll_top = 0;
  tb->scroll_bottom = tb->rows - 1;

  tb->using_alt_screen = false;
  mark_all_visible_dirty(tb);

  log_debug("XTERM: Exited alternate screen buffer, restored main screen");
}

/**
 * XTERM: Check if using alternate screen
 */
bool terminal_buffer_is_alt_screen(TerminalBuffer *tb) {
  if (!tb) {
    return false;
  }
  return tb->using_alt_screen;
}

/* ========================================================================
 * Scrollback View
 * ======================================================================== */

static const uint16_t *get_view_link_row(const TerminalBuffer *tb,
                                         size_t display_row) {
  if (!tb || display_row >= tb->rows || !tb->link_ids) {
    return nullptr;
  }

  if (tb->scroll_offset == 0 || !tb->scrollback_link_ids) {
    return &tb->link_ids[display_row * tb->cols];
  }

  size_t offset = tb->scroll_offset;
  if (offset > tb->scrollback_used) {
    offset = tb->scrollback_used;
  }

  size_t virtual_idx = tb->scrollback_used - offset + display_row;

  if (virtual_idx < tb->scrollback_used) {
    size_t ring_idx =
        (tb->scrollback_start + virtual_idx) % tb->scrollback_capacity;
    return &tb->scrollback_link_ids[ring_idx * tb->cols];
  }

  size_t screen_row = virtual_idx - tb->scrollback_used;
  if (screen_row >= tb->rows) {
    screen_row = tb->rows - 1;
  }
  return &tb->link_ids[screen_row * tb->cols];
}

void terminal_buffer_set_hyperlink(TerminalBuffer *tb,
                                   const char *uri_or_null) {
  if (!tb) {
    return;
  }
  if (!uri_or_null || uri_or_null[0] == '\0') {
    tb->current_link_id = 0;
    return;
  }
  tb->current_link_id = intern_uri(tb, uri_or_null);
}

const char *terminal_buffer_get_hyperlink_at(const TerminalBuffer *tb,
                                             size_t row, size_t col) {
  const uint16_t *ids;
  uint16_t id;

  if (!tb || col >= tb->cols) {
    return nullptr;
  }

  ids = get_view_link_row(tb, row);
  if (!ids) {
    return nullptr;
  }

  id = ids[col];
  if (id == 0 || id > tb->link_uri_count) {
    return nullptr;
  }
  return tb->link_uris[id - 1];
}

static bool view_cell_is_word(const TermCell *cell) {
  if (!cell) {
    return false;
  }
  if ((cell->flags & CELL_FLAG_CONTINUATION) != 0) {
    return true;
  }
  if (cell->codepoint == ' ' || cell->codepoint == 0) {
    return false;
  }
  return true;
}

static size_t snap_view_col(const TermCell *row_cells, size_t col) {
  while (col > 0 && (row_cells[col].flags & CELL_FLAG_CONTINUATION) != 0) {
    col--;
  }
  return col;
}

bool terminal_buffer_word_range(const TerminalBuffer *tb, size_t view_row,
                                size_t col, size_t *col_begin,
                                size_t *col_end) {
  const TermCell *row_cells;
  size_t cols;
  size_t begin;
  size_t end;

  if (!tb || !col_begin || !col_end) {
    return false;
  }
  cols = tb->cols;
  if (view_row >= tb->rows || col >= cols) {
    return false;
  }

  row_cells = terminal_buffer_get_view_row(tb, view_row);
  if (!row_cells) {
    return false;
  }

  col = snap_view_col(row_cells, col);
  if (!view_cell_is_word(&row_cells[col])) {
    *col_begin = col;
    *col_end = col;
    return true;
  }

  begin = col;
  while (begin > 0 && view_cell_is_word(&row_cells[begin - 1])) {
    begin--;
  }
  begin = snap_view_col(row_cells, begin);

  end = col + 1;
  while (end < cols && view_cell_is_word(&row_cells[end])) {
    end++;
  }

  *col_begin = begin;
  *col_end = end;
  return true;
}

bool terminal_buffer_line_range(const TerminalBuffer *tb, size_t view_row,
                                size_t *col_begin, size_t *col_end) {
  if (!tb || !col_begin || !col_end || view_row >= tb->rows) {
    return false;
  }
  *col_begin = 0;
  *col_end = tb->cols;
  return true;
}

const TermCell *terminal_buffer_get_view_row(const TerminalBuffer *tb,
                                             size_t display_row) {
  if (!tb || display_row >= tb->rows)
    return nullptr;

  if (tb->scroll_offset == 0 || !tb->scrollback) {
    return &tb->cells[display_row * tb->cols];
  }

  size_t offset = tb->scroll_offset;
  if (offset > tb->scrollback_used)
    offset = tb->scrollback_used;

  size_t virtual_idx = tb->scrollback_used - offset + display_row;

  if (virtual_idx < tb->scrollback_used) {
    size_t ring_idx =
        (tb->scrollback_start + virtual_idx) % tb->scrollback_capacity;
    return &tb->scrollback[ring_idx * tb->cols];
  }

  size_t screen_row = virtual_idx - tb->scrollback_used;
  if (screen_row >= tb->rows)
    screen_row = tb->rows - 1;
  return &tb->cells[screen_row * tb->cols];
}

void terminal_buffer_scroll_view(TerminalBuffer *tb, int delta) {
  if (!tb || tb->using_alt_screen)
    return;

  int new_offset = (int)tb->scroll_offset + delta;
  if (new_offset < 0)
    new_offset = 0;
  if ((size_t)new_offset > tb->scrollback_used)
    new_offset = (int)tb->scrollback_used;

  if ((size_t)new_offset != tb->scroll_offset) {
    tb->scroll_offset = (size_t)new_offset;
    mark_all_visible_dirty(tb);
  }
}

size_t terminal_buffer_get_scroll_offset(const TerminalBuffer *tb) {
  return tb ? tb->scroll_offset : 0;
}

size_t terminal_buffer_get_scrollback_used(const TerminalBuffer *tb) {
  return tb ? tb->scrollback_used : 0;
}

const TermCell *terminal_buffer_get_history_row(const TerminalBuffer *tb,
                                                size_t hist_row) {
  size_t used;
  size_t live_row;

  if (!tb) {
    return nullptr;
  }

  used = tb->scrollback_used;
  if (hist_row < used) {
    size_t ring_idx;
    if (!tb->scrollback || tb->scrollback_capacity == 0) {
      return nullptr;
    }
    ring_idx = (tb->scrollback_start + hist_row) % tb->scrollback_capacity;
    return &tb->scrollback[ring_idx * tb->cols];
  }

  live_row = hist_row - used;
  if (live_row >= tb->rows) {
    return nullptr;
  }
  return &tb->cells[live_row * tb->cols];
}

static size_t search_utf8_next(const char *s, uint32_t *out_cp) {
  const unsigned char *p = (const unsigned char *)s;
  unsigned char b0;
  size_t need;
  uint32_t cp;
  uint8_t expected;

  b0 = p[0];
  if (b0 == 0) {
    return 0;
  }
  if (b0 < 0x80U) {
    *out_cp = b0;
    return 1;
  }
  if ((b0 & 0xE0U) == 0xC0U) {
    need = 2;
    cp = b0 & 0x1FU;
    expected = 2;
  } else if ((b0 & 0xF0U) == 0xE0U) {
    need = 3;
    cp = b0 & 0x0FU;
    expected = 3;
  } else if ((b0 & 0xF8U) == 0xF0U) {
    need = 4;
    cp = b0 & 0x07U;
    expected = 4;
  } else {
    return 0;
  }

  for (size_t i = 1; i < need; i++) {
    if (p[i] == 0 || (p[i] & 0xC0U) != 0x80U) {
      return 0;
    }
    cp = (cp << 6) | (uint32_t)(p[i] & 0x3FU);
  }

  if (cp > 0x10FFFFU) {
    return 0;
  }
  if (cp >= 0xD800U && cp <= 0xDFFFU) {
    return 0;
  }
  if (expected == 2 && cp < 0x80U) {
    return 0;
  }
  if (expected == 3 && cp < 0x800U) {
    return 0;
  }
  if (expected == 4 && cp < 0x10000U) {
    return 0;
  }

  *out_cp = cp;
  return need;
}

static bool search_decode_query(const char *query, uint32_t *cps,
                                size_t *out_len) {
  size_t n = 0;
  const char *p = query;

  while (*p != '\0') {
    uint32_t cp;
    size_t used;

    if (n >= SEARCH_QUERY_MAX_CODEPOINTS) {
      return false;
    }
    used = search_utf8_next(p, &cp);
    if (used == 0) {
      return false;
    }
    cps[n++] = cp;
    p += used;
  }

  *out_len = n;
  return n > 0;
}

static bool search_cell_can_start(const TermCell *cell) {
  return (cell->flags & CELL_FLAG_CONTINUATION) == 0 && cell->width != 0;
}

static uint32_t search_ascii_fold(uint32_t cp) {
  if (cp >= (uint32_t)'A' && cp <= (uint32_t)'Z') {
    return cp + ((uint32_t)'a' - (uint32_t)'A');
  }
  return cp;
}

static bool search_codepoints_equal(uint32_t a, uint32_t b,
                                    bool case_insensitive) {
  if (a == b) {
    return true;
  }
  if (!case_insensitive) {
    return false;
  }
  return search_ascii_fold(a) == search_ascii_fold(b);
}

static bool search_match_at(const TermCell *row, size_t cols, size_t start_col,
                            const uint32_t *query, size_t qlen,
                            bool case_insensitive) {
  size_t qi = 0;
  size_t col = start_col;

  while (qi < qlen && col < cols) {
    const TermCell *cell = &row[col];
    if ((cell->flags & CELL_FLAG_CONTINUATION) != 0) {
      col++;
      continue;
    }
    if (!search_codepoints_equal(cell->codepoint, query[qi],
                                 case_insensitive)) {
      return false;
    }
    qi++;
    col++;
  }
  return qi == qlen;
}

static bool search_forward(const TerminalBuffer *tb, const uint32_t *query,
                           size_t qlen, bool case_insensitive, size_t start_row,
                           size_t start_col, size_t total, size_t cols,
                           size_t *out_row, size_t *out_col) {
  for (size_t row = start_row; row < total; row++) {
    const TermCell *cells = terminal_buffer_get_history_row(tb, row);
    size_t col_begin = (row == start_row) ? start_col : 0;

    if (!cells) {
      return false;
    }
    for (size_t col = col_begin; col < cols; col++) {
      if (!search_cell_can_start(&cells[col])) {
        continue;
      }
      if (search_match_at(cells, cols, col, query, qlen, case_insensitive)) {
        if (out_row) {
          *out_row = row;
        }
        if (out_col) {
          *out_col = col;
        }
        return true;
      }
    }
  }
  return false;
}

static bool search_backward(const TerminalBuffer *tb, const uint32_t *query,
                            size_t qlen, bool case_insensitive,
                            size_t start_row, size_t start_col, size_t cols,
                            size_t *out_row, size_t *out_col) {
  size_t row = start_row;

  for (;;) {
    const TermCell *cells = terminal_buffer_get_history_row(tb, row);
    size_t col = (row == start_row) ? start_col : cols - 1;

    if (!cells) {
      return false;
    }
    for (;;) {
      if (search_cell_can_start(&cells[col]) &&
          search_match_at(cells, cols, col, query, qlen, case_insensitive)) {
        if (out_row) {
          *out_row = row;
        }
        if (out_col) {
          *out_col = col;
        }
        return true;
      }
      if (col == 0) {
        break;
      }
      col--;
    }
    if (row == 0) {
      break;
    }
    row--;
  }
  return false;
}

bool terminal_buffer_search(const TerminalBuffer *tb, const char *query,
                            bool backwards, bool case_insensitive,
                            size_t start_row, size_t start_col, size_t *out_row,
                            size_t *out_col) {
  uint32_t cps[SEARCH_QUERY_MAX_CODEPOINTS];
  size_t qlen = 0;
  size_t total;
  size_t cols;

  if (!tb || !query) {
    return false;
  }
  if (!search_decode_query(query, cps, &qlen)) {
    return false;
  }

  cols = tb->cols;
  total = tb->scrollback_used + tb->rows;
  if (start_row >= total || start_col >= cols) {
    return false;
  }

  if (backwards) {
    return search_backward(tb, cps, qlen, case_insensitive, start_row,
                           start_col, cols, out_row, out_col);
  }
  return search_forward(tb, cps, qlen, case_insensitive, start_row, start_col,
                        total, cols, out_row, out_col);
}

MitrError terminal_buffer_set_scrollback_capacity(TerminalBuffer *tb,
                                                  size_t capacity) {
  if (!tb)
    return MITR_ERROR_INVALID_PARAM;

  if (!mitr_scrollback_capacity_valid(capacity)) {
    log_warn("Rejecting invalid scrollback capacity: %zu (max %d)", capacity,
             MITR_MAX_SCROLLBACK);
    return MITR_ERROR_INVALID_PARAM;
  }

  TermCell *new_scrollback = nullptr;
  uint16_t *new_scrollback_link_ids = nullptr;
  if (capacity > 0) {
    new_scrollback = alloc_scrollback(capacity, tb->cols);
    if (!new_scrollback) {
      log_warn("Failed to allocate scrollback capacity %zu", capacity);
      return MITR_ERROR_MEMORY;
    }
    new_scrollback_link_ids = alloc_scrollback_link_ids(capacity, tb->cols);
    if (!new_scrollback_link_ids) {
      free(new_scrollback);
      log_warn("Failed to allocate scrollback link ids for capacity %zu",
               capacity);
      return MITR_ERROR_MEMORY;
    }
  }

  SAFE_FREE(tb->scrollback);
  SAFE_FREE(tb->scrollback_link_ids);
  tb->scrollback = new_scrollback;
  tb->scrollback_link_ids = new_scrollback_link_ids;
  tb->scrollback_capacity = capacity;
  tb->scrollback_used = 0;
  tb->scrollback_start = 0;
  tb->scroll_offset = 0;
  mark_all_visible_dirty(tb);
  return MITR_OK;
}

/* ========================================================================
 * Accessors
 * ======================================================================== */

size_t terminal_buffer_get_rows(const TerminalBuffer *tb) {
  return tb ? tb->rows : 0;
}

size_t terminal_buffer_get_cols(const TerminalBuffer *tb) {
  return tb ? tb->cols : 0;
}

void terminal_buffer_get_dimensions(const TerminalBuffer *tb, size_t *rows,
                                    size_t *cols) {
  if (!tb)
    return;
  if (rows)
    *rows = tb->rows;
  if (cols)
    *cols = tb->cols;
}

bool terminal_buffer_is_reverse_screen(const TerminalBuffer *tb) {
  return tb && tb->reverse_screen;
}

uint32_t terminal_buffer_get_config_fg(const TerminalBuffer *tb) {
  return tb ? tb->config_fg : COLOR_DEFAULT_FG;
}

uint32_t terminal_buffer_get_config_bg(const TerminalBuffer *tb) {
  return tb ? tb->config_bg : COLOR_DEFAULT_BG;
}

bool terminal_buffer_is_cursor_visible(const TerminalBuffer *tb) {
  return tb && tb->cursor_visible;
}

uint32_t terminal_buffer_get_default_fg(const TerminalBuffer *tb) {
  return tb ? tb->default_fg : COLOR_DEFAULT_FG;
}

uint32_t terminal_buffer_get_default_bg(const TerminalBuffer *tb) {
  return tb ? tb->default_bg : COLOR_DEFAULT_BG;
}

uint8_t terminal_buffer_get_current_attr(const TerminalBuffer *tb) {
  return tb ? tb->current_attr : ATTR_NONE;
}

void terminal_buffer_get_scroll_region(const TerminalBuffer *tb, size_t *top,
                                       size_t *bottom) {
  if (!tb)
    return;
  if (top)
    *top = tb->scroll_top;
  if (bottom)
    *bottom = tb->scroll_bottom;
}

bool terminal_buffer_is_origin_mode(const TerminalBuffer *tb) {
  return tb && tb->origin_mode;
}

bool terminal_buffer_is_auto_wrap(const TerminalBuffer *tb) {
  return tb && tb->auto_wrap;
}

bool terminal_buffer_is_pending_wrap(const TerminalBuffer *tb) {
  return tb && tb->pending_wrap;
}

size_t terminal_buffer_get_scrollback_capacity(const TerminalBuffer *tb) {
  return tb ? tb->scrollback_capacity : 0;
}

bool terminal_buffer_has_scrollback(const TerminalBuffer *tb) {
  return tb && tb->scrollback != nullptr;
}

void terminal_buffer_set_default_fg(TerminalBuffer *tb, uint32_t fg) {
  if (tb)
    tb->default_fg = fg;
}

void terminal_buffer_set_default_bg(TerminalBuffer *tb, uint32_t bg) {
  if (tb)
    tb->default_bg = bg;
}

void terminal_buffer_set_current_attr(TerminalBuffer *tb, uint8_t attr) {
  if (tb)
    tb->current_attr = attr;
}

void terminal_buffer_set_pending_wrap(TerminalBuffer *tb, bool pending) {
  if (tb)
    tb->pending_wrap = pending;
}

void terminal_buffer_set_origin_mode(TerminalBuffer *tb, bool enabled) {
  if (tb)
    tb->origin_mode = enabled;
}

void terminal_buffer_set_auto_wrap(TerminalBuffer *tb, bool enabled) {
  if (tb)
    tb->auto_wrap = enabled;
}

void terminal_buffer_set_cursor_visible(TerminalBuffer *tb, bool visible) {
  if (!tb || tb->cursor_visible == visible) {
    return;
  }
  tb->cursor_visible = visible;
  mark_live_row_dirty(tb, tb->cursor_row);
}

void terminal_buffer_set_reverse_screen(TerminalBuffer *tb, bool enabled) {
  if (!tb || tb->reverse_screen == enabled) {
    return;
  }
  tb->reverse_screen = enabled;
  mark_all_visible_dirty(tb);
}

void terminal_buffer_set_insert_mode(TerminalBuffer *tb, bool enabled) {
  if (tb)
    tb->insert_mode = enabled;
}

bool terminal_buffer_is_insert_mode(const TerminalBuffer *tb) {
  return tb && tb->insert_mode;
}
