/**
 * test_terminal_buffer.c - Tests for Terminal Buffer
 */

#include "terminal_buffer.h"
#include "test_framework.h"

/* ========================================================================
 * Initialization and Destruction Tests
 * ======================================================================== */

TEST(terminal_buffer_init_creates_buffer) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  ASSERT_NOT_NULL(tb);
  ASSERT_EQ(terminal_buffer_get_rows(tb), 24);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 80);
  ASSERT_NOT_NULL(terminal_buffer_get_cell(tb, 0, 0));
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);
  ASSERT_TRUE(terminal_buffer_is_cursor_visible(tb));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_init_with_default_colors) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  ASSERT_NOT_NULL(tb);
  ASSERT_EQ(terminal_buffer_get_default_fg(tb), COLOR_DEFAULT_FG);
  ASSERT_EQ(terminal_buffer_get_default_bg(tb), COLOR_DEFAULT_BG);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_get_next_tab_stop_handles_null) {
  ASSERT_EQ(terminal_buffer_get_next_tab_stop(nullptr), 0);
}

TEST(terminal_buffer_get_prev_tab_stop_handles_null) {
  ASSERT_EQ(terminal_buffer_get_prev_tab_stop(nullptr), 0);
}

TEST(terminal_buffer_get_prev_tab_stop_default_stops) {
  TerminalBuffer *tb = terminal_buffer_init(10, 24);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_set_cursor(tb, 0, 10);
  ASSERT_EQ(terminal_buffer_get_prev_tab_stop(tb), 8);

  terminal_buffer_set_cursor(tb, 3, 10);
  ASSERT_EQ(terminal_buffer_get_prev_tab_stop(tb), 8);

  terminal_buffer_set_cursor(tb, 0, 0);
  ASSERT_EQ(terminal_buffer_get_prev_tab_stop(tb), 0);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_insert_mode_shifts_then_writes) {
  TerminalBuffer *tb = terminal_buffer_init(4, 10);
  ASSERT_NOT_NULL(tb);
  ASSERT_FALSE(terminal_buffer_is_insert_mode(tb));

  terminal_buffer_put_char(tb, 'A');
  terminal_buffer_put_char(tb, 'B');
  terminal_buffer_put_char(tb, 'C');
  terminal_buffer_put_char(tb, 'D');
  terminal_buffer_set_cursor(tb, 0, 2);
  terminal_buffer_set_insert_mode(tb, true);
  ASSERT_TRUE(terminal_buffer_is_insert_mode(tb));

  terminal_buffer_put_char(tb, 'X');

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, (uint32_t)'B');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, (uint32_t)'X');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 3)->codepoint, (uint32_t)'C');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 4)->codepoint, (uint32_t)'D');

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_insert_mode_combining_does_not_shift) {
  TerminalBuffer *tb = terminal_buffer_init(4, 10);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_put_char(tb, 'e');
  terminal_buffer_put_char(tb, 'C');
  terminal_buffer_set_cursor(tb, 0, 1);
  terminal_buffer_set_insert_mode(tb, true);
  terminal_buffer_put_codepoint(tb, 0x0301);

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'e');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, 0x0301u);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, (uint32_t)' ');

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_insert_mode_wide_cjk) {
  TerminalBuffer *tb = terminal_buffer_init(4, 10);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_put_char(tb, 'A');
  terminal_buffer_put_char(tb, 'B');
  terminal_buffer_put_char(tb, 'C');
  terminal_buffer_put_char(tb, 'D');
  terminal_buffer_set_cursor(tb, 0, 2);
  terminal_buffer_set_insert_mode(tb, true);
  terminal_buffer_put_codepoint(tb, 0x4E00);

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, (uint32_t)'B');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, 0x4E00u);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->width, 2);
  ASSERT_TRUE((terminal_buffer_get_cell(tb, 0, 3)->flags &
               CELL_FLAG_CONTINUATION) != 0);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 4)->codepoint, (uint32_t)'C');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 5)->codepoint, (uint32_t)'D');

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_insert_mode_last_column) {
  TerminalBuffer *tb = terminal_buffer_init(3, 5);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_put_char(tb, 'A');
  terminal_buffer_put_char(tb, 'B');
  terminal_buffer_put_char(tb, 'C');
  terminal_buffer_put_char(tb, 'D');
  terminal_buffer_put_char(tb, 'E');
  terminal_buffer_set_cursor(tb, 0, 4);
  terminal_buffer_set_insert_mode(tb, true);
  terminal_buffer_put_char(tb, 'X');

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 3)->codepoint, (uint32_t)'D');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 4)->codepoint, (uint32_t)'X');

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_soft_reset_clears_insert_mode) {
  TerminalBuffer *tb = terminal_buffer_init(4, 10);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_set_insert_mode(tb, true);
  ASSERT_TRUE(terminal_buffer_is_insert_mode(tb));
  terminal_buffer_soft_reset(tb);
  ASSERT_FALSE(terminal_buffer_is_insert_mode(tb));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_destroy_handles_null) {
  /* Should not crash */
  terminal_buffer_destroy(nullptr);
  ASSERT_TRUE(true);
}

/* ========================================================================
 * Cell Access Tests
 * ======================================================================== */

TEST(terminal_buffer_get_cell_returns_valid_cell) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_NOT_NULL(cell);

  cell = terminal_buffer_get_cell(tb, 23, 79);
  ASSERT_NOT_NULL(cell);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_get_cell_returns_null_for_out_of_bounds) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  TermCell *cell = terminal_buffer_get_cell(tb, 24, 0);
  ASSERT_NULL(cell);

  cell = terminal_buffer_get_cell(tb, 0, 80);
  ASSERT_NULL(cell);

  cell = terminal_buffer_get_cell(tb, 100, 100);
  ASSERT_NULL(cell);

  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Clear Tests
 * ======================================================================== */

TEST(terminal_buffer_clear_fills_with_spaces) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  terminal_buffer_clear(tb);

  /* Check first cell */
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_NOT_NULL(cell);
  ASSERT_EQ(cell->codepoint, ' ');
  ASSERT_EQ(cell->fg_color, COLOR_DEFAULT_FG);
  ASSERT_EQ(cell->bg_color, COLOR_DEFAULT_BG);

  /* Check last cell */
  cell = terminal_buffer_get_cell(tb, 23, 79);
  ASSERT_NOT_NULL(cell);
  ASSERT_EQ(cell->codepoint, ' ');

  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Cursor Tests
 * ======================================================================== */

TEST(terminal_buffer_cursor_position) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  terminal_buffer_set_cursor(tb, 10, 20);
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 10);
  ASSERT_EQ(col, 20);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_set_cursor_clamps_to_bounds) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  /* Set beyond bounds */
  terminal_buffer_set_cursor(tb, 100, 100);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);

  /* Should be clamped */
  ASSERT_TRUE(row < 24);
  ASSERT_TRUE(col < 80);

  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Put Char Tests
 * ======================================================================== */

TEST(terminal_buffer_put_char_writes_character) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_char(tb, 'A');

  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_NOT_NULL(cell);
  ASSERT_EQ(cell->codepoint, 'A');

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_put_char_advances_cursor) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_char(tb, 'H');

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 1);

  terminal_buffer_put_char(tb, 'i');
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 2);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_put_char_wraps_at_end_of_line) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Move to end of line */
  terminal_buffer_set_cursor(tb, 0, 79);
  terminal_buffer_put_char(tb, 'X');

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);

  /* VT100 pending wrap: cursor stays at last column until next char */
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 79);
  ASSERT_TRUE(terminal_buffer_is_pending_wrap(tb));

  /* Writing another character triggers the deferred wrap */
  terminal_buffer_put_char(tb, 'Y');
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 1);
  ASSERT_EQ(col, 1);
  ASSERT_FALSE(terminal_buffer_is_pending_wrap(tb));

  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Scroll Tests
 * ======================================================================== */

TEST(terminal_buffer_scroll_up_moves_content) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Write 'A' in row 1 */
  terminal_buffer_set_cursor(tb, 1, 0);
  terminal_buffer_put_char(tb, 'A');

  /* Scroll up */
  terminal_buffer_scroll_up(tb);

  /* 'A' should now be in row 0 */
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_NOT_NULL(cell);
  ASSERT_EQ(cell->codepoint, 'A');

  /* Row 1 should be cleared */
  cell = terminal_buffer_get_cell(tb, 1, 0);
  ASSERT_NOT_NULL(cell);
  ASSERT_EQ(cell->codepoint, ' ');

  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Dimension / Resize Safety Tests
 * ======================================================================== */

TEST(terminal_buffer_init_rejects_zero_dimensions) {
  ASSERT_NULL(terminal_buffer_init(0, 80));
  ASSERT_NULL(terminal_buffer_init(24, 0));
  ASSERT_NULL(terminal_buffer_init(0, 0));
}

TEST(terminal_buffer_init_rejects_oversized_dimensions) {
  ASSERT_NULL(terminal_buffer_init(MITR_MAX_ROWS + 1, 80));
  ASSERT_NULL(terminal_buffer_init(24, MITR_MAX_COLS + 1));
}

TEST(terminal_buffer_resize_rejects_invalid_dimensions) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);

  ASSERT_EQ(terminal_buffer_resize(tb, 0, 80), MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(terminal_buffer_resize(tb, 24, 0), MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(terminal_buffer_resize(tb, MITR_MAX_ROWS + 1, 80),
            MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(terminal_buffer_resize(tb, 24, MITR_MAX_COLS + 1),
            MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(terminal_buffer_resize(tb, SIZE_MAX, SIZE_MAX),
            MITR_ERROR_INVALID_PARAM);

  /* Failed resizes must leave the buffer dimensions unchanged */
  ASSERT_EQ(terminal_buffer_get_rows(tb), 24);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 80);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_resize_with_alt_screen_keeps_matching_size) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);

  ASSERT_EQ(terminal_buffer_enter_alt_screen(tb), MITR_OK);
  ASSERT_TRUE(terminal_buffer_is_alt_screen(tb));
  ASSERT_NOT_NULL(terminal_buffer_get_cell(tb, 0, 0));

  ASSERT_EQ(terminal_buffer_resize(tb, 40, 100), MITR_OK);
  ASSERT_EQ(terminal_buffer_get_rows(tb), 40);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 100);
  ASSERT_NOT_NULL(terminal_buffer_get_cell(tb, 39, 99));

  /* Active screen (alt) must be addressable at the new size */
  TermCell *cell = terminal_buffer_get_cell(tb, 39, 99);
  ASSERT_NOT_NULL(cell);
  cell->codepoint = 'A';

  terminal_buffer_exit_alt_screen(tb);
  ASSERT_FALSE(terminal_buffer_is_alt_screen(tb));
  ASSERT_EQ(terminal_buffer_get_rows(tb), 40);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 100);

  /* Main screen (restored) must also match the new size */
  cell = terminal_buffer_get_cell(tb, 39, 99);
  ASSERT_NOT_NULL(cell);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_resize_failure_with_alt_screen_is_transactional) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);

  ASSERT_EQ(terminal_buffer_enter_alt_screen(tb), MITR_OK);
  ASSERT_NOT_NULL(terminal_buffer_get_cell(tb, 0, 0));

  /* Overflow / oversize request must fail without mutating state */
  ASSERT_EQ(terminal_buffer_resize(tb, SIZE_MAX / 2, SIZE_MAX / 2),
            MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(terminal_buffer_get_rows(tb), 24);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 80);
  ASSERT_TRUE(terminal_buffer_is_alt_screen(tb));
  ASSERT_NOT_NULL(terminal_buffer_get_cell(tb, 23, 79));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_resize_columns_with_alt_screen_is_transactional) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);

  ASSERT_EQ(terminal_buffer_enter_alt_screen(tb), MITR_OK);
  ASSERT_EQ(terminal_buffer_resize_columns(tb, 132), MITR_OK);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 132);
  ASSERT_EQ(terminal_buffer_get_rows(tb), 24);
  ASSERT_NOT_NULL(terminal_buffer_get_cell(tb, 0, 131));

  TermCell *cell = terminal_buffer_get_cell(tb, 0, 131);
  ASSERT_NOT_NULL(cell);

  ASSERT_EQ(terminal_buffer_resize_columns(tb, MITR_MAX_COLS + 1),
            MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 132);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_set_scrollback_capacity_rejects_invalid) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);

  size_t old_capacity = terminal_buffer_get_scrollback_capacity(tb);
  ASSERT_EQ(
      terminal_buffer_set_scrollback_capacity(tb, MITR_MAX_SCROLLBACK + 1),
      MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(terminal_buffer_get_scrollback_capacity(tb), old_capacity);

  ASSERT_EQ(terminal_buffer_set_scrollback_capacity(tb, 128), MITR_OK);
  ASSERT_EQ(terminal_buffer_get_scrollback_capacity(tb), 128);
  ASSERT_TRUE(terminal_buffer_has_scrollback(tb));

  ASSERT_EQ(terminal_buffer_set_scrollback_capacity(tb, 0), MITR_OK);
  ASSERT_EQ(terminal_buffer_get_scrollback_capacity(tb), 0);
  ASSERT_FALSE(terminal_buffer_has_scrollback(tb));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_scroll_view_on_main_increases_offset) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);
  ASSERT_TRUE(terminal_buffer_has_scrollback(tb));

  for (int i = 0; i < 5; i++) {
    terminal_buffer_scroll_up(tb);
  }
  ASSERT_EQ(terminal_buffer_get_scroll_offset(tb), 0);

  terminal_buffer_scroll_view(tb, 3);
  ASSERT_EQ(terminal_buffer_get_scroll_offset(tb), 3);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_hyperlink_survives_scrollback) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);
  ASSERT_TRUE(terminal_buffer_has_scrollback(tb));

  terminal_buffer_set_hyperlink(tb, "https://example.com");
  terminal_buffer_put_char(tb, 'A');
  terminal_buffer_put_char(tb, 'B');

  ASSERT_STR_EQ(terminal_buffer_get_hyperlink_at(tb, 0, 0),
                "https://example.com");

  terminal_buffer_scroll_up(tb);
  ASSERT_NULL(terminal_buffer_get_hyperlink_at(tb, 0, 0));

  terminal_buffer_scroll_view(tb, 1);
  ASSERT_EQ(terminal_buffer_get_scroll_offset(tb), 1);
  ASSERT_STR_EQ(terminal_buffer_get_hyperlink_at(tb, 0, 0),
                "https://example.com");
  ASSERT_STR_EQ(terminal_buffer_get_hyperlink_at(tb, 0, 1),
                "https://example.com");

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_scroll_view_on_alt_screen_is_noop) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);
  ASSERT_TRUE(terminal_buffer_has_scrollback(tb));

  for (int i = 0; i < 5; i++) {
    terminal_buffer_scroll_up(tb);
  }
  ASSERT_EQ(terminal_buffer_get_scroll_offset(tb), 0);

  ASSERT_EQ(terminal_buffer_enter_alt_screen(tb), MITR_OK);
  ASSERT_TRUE(terminal_buffer_is_alt_screen(tb));

  terminal_buffer_scroll_view(tb, 3);
  ASSERT_EQ(terminal_buffer_get_scroll_offset(tb), 0);

  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * History addressing and allocation-free search
 * ======================================================================== */

static void put_str(TerminalBuffer *tb, const char *s) {
  while (*s != '\0') {
    terminal_buffer_put_char(tb, *s);
    s++;
  }
}

TEST(terminal_buffer_search_finds_on_live_screen) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  size_t row = 99;
  size_t col = 99;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "hello");

  ASSERT_TRUE(
      terminal_buffer_search(tb, "ell", false, false, 0, 0, &row, &col));
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 1);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_search_finds_in_scrollback) {
  TerminalBuffer *tb = terminal_buffer_init(4, 20);
  const TermCell *hist;
  size_t row = 99;
  size_t col = 99;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "needle");

  for (size_t i = 0; i < terminal_buffer_get_rows(tb); i++) {
    terminal_buffer_scroll_up(tb);
  }

  ASSERT_EQ(terminal_buffer_get_scrollback_used(tb), 4);
  hist = terminal_buffer_get_history_row(tb, 0);
  ASSERT_NOT_NULL(hist);
  ASSERT_EQ(hist[0].codepoint, 'n');

  ASSERT_TRUE(
      terminal_buffer_search(tb, "needle", false, false, 0, 0, &row, &col));
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_search_forward_and_backward) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  size_t first_row = 99;
  size_t first_col = 99;
  size_t second_row = 99;
  size_t second_col = 99;
  size_t row = 99;
  size_t col = 99;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "foo bar foo");

  ASSERT_TRUE(terminal_buffer_search(tb, "foo", false, false, 0, 0, &first_row,
                                     &first_col));
  ASSERT_EQ(first_row, 0);
  ASSERT_EQ(first_col, 0);

  /* Forward from after the first hit finds the second. */
  ASSERT_TRUE(terminal_buffer_search(tb, "foo", false, false, first_row,
                                     first_col + 1, &second_row, &second_col));
  ASSERT_EQ(second_row, 0);
  ASSERT_EQ(second_col, 8);

  /* Start is inclusive, so walk left from the cell before the second hit. */
  ASSERT_TRUE(terminal_buffer_search(tb, "foo", true, false, second_row,
                                     second_col - 1, &row, &col));
  ASSERT_EQ(row, first_row);
  ASSERT_EQ(col, first_col);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_search_no_match) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  size_t row = 7;
  size_t col = 7;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "hello");

  ASSERT_FALSE(
      terminal_buffer_search(tb, "xyz", false, false, 0, 0, &row, &col));
  ASSERT_FALSE(
      terminal_buffer_search(tb, "hello", false, false, 100, 0, &row, &col));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_search_wrap_restarts_at_origin) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  size_t first_row = 99;
  size_t first_col = 99;
  size_t second_row = 99;
  size_t second_col = 99;
  size_t row = 99;
  size_t col = 99;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "foo bar foo");

  ASSERT_TRUE(terminal_buffer_search(tb, "foo", false, false, 0, 0, &first_row,
                                     &first_col));
  ASSERT_TRUE(terminal_buffer_search(tb, "foo", false, false, first_row,
                                     first_col + 1, &second_row, &second_col));
  ASSERT_FALSE(terminal_buffer_search(tb, "foo", false, false, second_row,
                                      second_col + 1, &row, &col));
  /* UI wrap (app_search): no more hits, so search again from (0,0). */
  ASSERT_TRUE(
      terminal_buffer_search(tb, "foo", false, false, 0, 0, &row, &col));
  ASSERT_EQ(row, first_row);
  ASSERT_EQ(col, first_col);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_search_empty_query) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  size_t row = 0;
  size_t col = 0;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "hello");

  ASSERT_FALSE(terminal_buffer_search(tb, "", false, false, 0, 0, &row, &col));
  ASSERT_FALSE(
      terminal_buffer_search(tb, nullptr, false, false, 0, 0, &row, &col));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_search_case_sensitive_rejects_hello) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  size_t row = 99;
  size_t col = 99;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "Hello");

  ASSERT_FALSE(
      terminal_buffer_search(tb, "hello", false, false, 0, 0, &row, &col));
  ASSERT_FALSE(
      terminal_buffer_search(tb, "HELLO", false, false, 0, 0, &row, &col));
  ASSERT_TRUE(
      terminal_buffer_search(tb, "Hello", false, false, 0, 0, &row, &col));
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_search_case_insensitive_ascii_fold) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  size_t row = 99;
  size_t col = 99;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "Hello");

  ASSERT_TRUE(
      terminal_buffer_search(tb, "hello", false, true, 0, 0, &row, &col));
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  row = 99;
  col = 99;
  ASSERT_TRUE(
      terminal_buffer_search(tb, "HELLO", false, true, 0, 0, &row, &col));
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_put_dirties_one_row) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_clear_dirty(tb);
  terminal_buffer_put_char(tb, 'A');
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 3));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 4));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(nullptr, 0));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_index_scroll_dirties_region) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_clear_dirty(tb);
  terminal_buffer_index(tb);
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 3));

  terminal_buffer_set_cursor(tb, 3, 0);
  terminal_buffer_clear_dirty(tb);
  terminal_buffer_index(tb);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 3));

  terminal_buffer_clear_dirty(tb);
  terminal_buffer_scroll_up(tb);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 3));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_scroll_up_dirties_vacated_row) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  size_t top = 99;
  size_t bottom = 99;
  int delta = 0;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_clear_dirty(tb);
  ASSERT_FALSE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_FALSE(
      terminal_buffer_take_scroll_shift(nullptr, &top, &bottom, &delta));

  terminal_buffer_scroll_up(tb);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 3));

  ASSERT_TRUE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_EQ(top, 0);
  ASSERT_EQ(bottom, 3);
  ASSERT_EQ(delta, 1);
  ASSERT_FALSE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_scroll_shift_coalesces) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  size_t top = 0;
  size_t bottom = 0;
  int delta = 0;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_clear_dirty(tb);
  terminal_buffer_scroll_up(tb);
  terminal_buffer_scroll_up(tb);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 3));

  ASSERT_TRUE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_EQ(top, 0);
  ASSERT_EQ(bottom, 3);
  ASSERT_EQ(delta, 2);
  ASSERT_FALSE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_clear_dirty_drops_scroll_shift) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  size_t top = 0;
  size_t bottom = 0;
  int delta = 0;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_clear_dirty(tb);
  terminal_buffer_scroll_up(tb);
  terminal_buffer_clear_dirty(tb);
  ASSERT_FALSE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 3));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_mark_all_dirty_drops_scroll_shift) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  size_t top = 0;
  size_t bottom = 0;
  int delta = 0;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_clear_dirty(tb);
  terminal_buffer_scroll_up(tb);
  terminal_buffer_mark_all_dirty(tb);
  ASSERT_FALSE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 3));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_scroll_up_dirties_only_region_vacated) {
  TerminalBuffer *tb = terminal_buffer_init(5, 8);
  size_t top = 99;
  size_t bottom = 99;
  int delta = 0;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_scroll_region(tb, 1, 3);
  terminal_buffer_clear_dirty(tb);
  terminal_buffer_scroll_up(tb);

  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 3));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 4));

  ASSERT_TRUE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_EQ(top, 1);
  ASSERT_EQ(bottom, 3);
  ASSERT_EQ(delta, 1);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_scroll_down_dirties_vacated_top) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  size_t top = 99;
  size_t bottom = 99;
  int delta = 0;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_clear_dirty(tb);
  terminal_buffer_scroll_down(tb);
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 3));

  ASSERT_TRUE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_EQ(top, 0);
  ASSERT_EQ(bottom, 3);
  ASSERT_EQ(delta, -1);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_insert_delete_lines_record_shift) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  size_t top = 99;
  size_t bottom = 99;
  int delta = 0;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 1, 0);
  terminal_buffer_clear_dirty(tb);
  terminal_buffer_insert_lines(tb, 1);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 3));
  ASSERT_TRUE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_EQ(top, 1);
  ASSERT_EQ(bottom, 3);
  ASSERT_EQ(delta, -1);

  terminal_buffer_clear_dirty(tb);
  terminal_buffer_delete_lines(tb, 1);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 2));
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 3));
  ASSERT_TRUE(terminal_buffer_take_scroll_shift(tb, &top, &bottom, &delta));
  ASSERT_EQ(top, 1);
  ASSERT_EQ(bottom, 3);
  ASSERT_EQ(delta, 1);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_clear_dirty_then_put_dirties_again) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_put_char(tb, 'A');
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 0));
  terminal_buffer_clear_dirty(tb);
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));
  terminal_buffer_put_char(tb, 'B');
  ASSERT_TRUE(terminal_buffer_row_is_dirty(tb, 0));
  ASSERT_FALSE(terminal_buffer_row_is_dirty(tb, 1));

  terminal_buffer_clear_dirty(nullptr);

  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Word / line range (view coordinates, half-open [begin, end))
 * ======================================================================== */

TEST(terminal_buffer_word_range_around_spaces) {
  TerminalBuffer *tb = terminal_buffer_init(3, 20);
  size_t begin = 99;
  size_t end = 99;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "hello world");

  ASSERT_TRUE(terminal_buffer_word_range(tb, 0, 3, &begin, &end));
  ASSERT_EQ(begin, 0);
  ASSERT_EQ(end, 5);

  ASSERT_TRUE(terminal_buffer_word_range(tb, 0, 0, &begin, &end));
  ASSERT_EQ(begin, 0);
  ASSERT_EQ(end, 5);

  ASSERT_TRUE(terminal_buffer_word_range(tb, 0, 4, &begin, &end));
  ASSERT_EQ(begin, 0);
  ASSERT_EQ(end, 5);

  ASSERT_TRUE(terminal_buffer_word_range(tb, 0, 8, &begin, &end));
  ASSERT_EQ(begin, 6);
  ASSERT_EQ(end, 11);

  ASSERT_TRUE(terminal_buffer_word_range(tb, 0, 5, &begin, &end));
  ASSERT_EQ(begin, 5);
  ASSERT_EQ(end, 5);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_word_range_empty_cell) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  size_t begin = 99;
  size_t end = 99;

  ASSERT_NOT_NULL(tb);
  ASSERT_TRUE(terminal_buffer_word_range(tb, 0, 4, &begin, &end));
  ASSERT_EQ(begin, 4);
  ASSERT_EQ(end, 4);

  ASSERT_FALSE(terminal_buffer_word_range(nullptr, 0, 0, &begin, &end));
  ASSERT_FALSE(terminal_buffer_word_range(tb, 0, 0, nullptr, &end));
  ASSERT_FALSE(terminal_buffer_word_range(tb, 99, 0, &begin, &end));

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_word_range_wide_char_begins_on_base) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  size_t begin = 99;
  size_t end = 99;

  ASSERT_NOT_NULL(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_codepoint(tb, 0x4E00); /* 一, width 2 */
  terminal_buffer_put_char(tb, ' ');
  terminal_buffer_put_char(tb, 'Z');

  ASSERT_TRUE(terminal_buffer_word_range(tb, 0, 1, &begin, &end));
  ASSERT_EQ(begin, 0);
  ASSERT_EQ(end, 2);
  ASSERT_TRUE((terminal_buffer_get_cell(tb, 0, 1)->flags &
               CELL_FLAG_CONTINUATION) != 0);

  ASSERT_TRUE(terminal_buffer_word_range(tb, 0, 0, &begin, &end));
  ASSERT_EQ(begin, 0);
  ASSERT_EQ(end, 2);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_line_range_covers_whole_row) {
  TerminalBuffer *tb = terminal_buffer_init(4, 12);
  size_t begin = 99;
  size_t end = 99;

  ASSERT_NOT_NULL(tb);
  ASSERT_TRUE(terminal_buffer_line_range(tb, 2, &begin, &end));
  ASSERT_EQ(begin, 0);
  ASSERT_EQ(end, 12);

  ASSERT_FALSE(terminal_buffer_line_range(nullptr, 0, &begin, &end));
  ASSERT_FALSE(terminal_buffer_line_range(tb, 4, &begin, &end));

  terminal_buffer_destroy(tb);
}

void run_terminal_buffer_tests(void) {
  printf("\n" TERM_COLOR_YELLOW "=== Terminal Buffer Tests ===" TERM_COLOR_RESET
         "\n");
  RUN_TEST(terminal_buffer_init_creates_buffer);
  RUN_TEST(terminal_buffer_init_with_default_colors);
  RUN_TEST(terminal_buffer_destroy_handles_null);
  RUN_TEST(terminal_buffer_get_next_tab_stop_handles_null);
  RUN_TEST(terminal_buffer_get_prev_tab_stop_handles_null);
  RUN_TEST(terminal_buffer_get_prev_tab_stop_default_stops);
  RUN_TEST(terminal_buffer_insert_mode_shifts_then_writes);
  RUN_TEST(terminal_buffer_insert_mode_combining_does_not_shift);
  RUN_TEST(terminal_buffer_insert_mode_wide_cjk);
  RUN_TEST(terminal_buffer_insert_mode_last_column);
  RUN_TEST(terminal_buffer_soft_reset_clears_insert_mode);
  RUN_TEST(terminal_buffer_get_cell_returns_valid_cell);
  RUN_TEST(terminal_buffer_get_cell_returns_null_for_out_of_bounds);
  RUN_TEST(terminal_buffer_clear_fills_with_spaces);
  RUN_TEST(terminal_buffer_cursor_position);
  RUN_TEST(terminal_buffer_set_cursor_clamps_to_bounds);
  RUN_TEST(terminal_buffer_put_char_writes_character);
  RUN_TEST(terminal_buffer_put_char_advances_cursor);
  RUN_TEST(terminal_buffer_put_char_wraps_at_end_of_line);
  RUN_TEST(terminal_buffer_scroll_up_moves_content);
  RUN_TEST(terminal_buffer_init_rejects_zero_dimensions);
  RUN_TEST(terminal_buffer_init_rejects_oversized_dimensions);
  RUN_TEST(terminal_buffer_resize_rejects_invalid_dimensions);
  RUN_TEST(terminal_buffer_resize_with_alt_screen_keeps_matching_size);
  RUN_TEST(terminal_buffer_resize_failure_with_alt_screen_is_transactional);
  RUN_TEST(terminal_buffer_resize_columns_with_alt_screen_is_transactional);
  RUN_TEST(terminal_buffer_set_scrollback_capacity_rejects_invalid);
  RUN_TEST(terminal_buffer_scroll_view_on_main_increases_offset);
  RUN_TEST(terminal_buffer_hyperlink_survives_scrollback);
  RUN_TEST(terminal_buffer_scroll_view_on_alt_screen_is_noop);
  RUN_TEST(terminal_buffer_search_finds_on_live_screen);
  RUN_TEST(terminal_buffer_search_finds_in_scrollback);
  RUN_TEST(terminal_buffer_search_forward_and_backward);
  RUN_TEST(terminal_buffer_search_wrap_restarts_at_origin);
  RUN_TEST(terminal_buffer_search_no_match);
  RUN_TEST(terminal_buffer_search_empty_query);
  RUN_TEST(terminal_buffer_search_case_sensitive_rejects_hello);
  RUN_TEST(terminal_buffer_search_case_insensitive_ascii_fold);
  RUN_TEST(terminal_buffer_put_dirties_one_row);
  RUN_TEST(terminal_buffer_index_scroll_dirties_region);
  RUN_TEST(terminal_buffer_scroll_up_dirties_vacated_row);
  RUN_TEST(terminal_buffer_scroll_shift_coalesces);
  RUN_TEST(terminal_buffer_clear_dirty_drops_scroll_shift);
  RUN_TEST(terminal_buffer_mark_all_dirty_drops_scroll_shift);
  RUN_TEST(terminal_buffer_scroll_up_dirties_only_region_vacated);
  RUN_TEST(terminal_buffer_scroll_down_dirties_vacated_top);
  RUN_TEST(terminal_buffer_insert_delete_lines_record_shift);
  RUN_TEST(terminal_buffer_clear_dirty_then_put_dirties_again);
  RUN_TEST(terminal_buffer_word_range_around_spaces);
  RUN_TEST(terminal_buffer_word_range_empty_cell);
  RUN_TEST(terminal_buffer_word_range_wide_char_begins_on_base);
  RUN_TEST(terminal_buffer_line_range_covers_whole_row);
}
