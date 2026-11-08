/**
 * test_vt100.c - Tests for VT100 standard implementation
 *
 * Tests for VT100 (1978) terminal standard compliance
 */

#include "ansi_parser.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include "test_helpers.h"
#include <string.h>

typedef struct {
  char buf[64];
  size_t len;
} ReplyCap;

static void capture_reply(void *ud, const char *data, size_t len) {
  ReplyCap *cap = ud;
  if (!cap || !data) {
    return;
  }
  size_t n = len;
  if (n >= sizeof(cap->buf)) {
    n = sizeof(cap->buf) - 1;
  }
  memcpy(cap->buf, data, n);
  cap->buf[n] = '\0';
  cap->len = n;
}

static void reply_cap_clear(ReplyCap *cap) {
  cap->buf[0] = '\0';
  cap->len = 0;
}

/* ========================================================================
 * VT100: DECOM (Origin Mode) Tests - ESC[?6h/l
 * ======================================================================== */

TEST(vt100_decom_disabled_absolute_positioning) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set scrolling region to rows 2-7 */
  process_ansi_string(parser, "\033[3;8r", tb);

  /* Origin mode off (default) - absolute positioning */
  ASSERT_FALSE(terminal_buffer_is_origin_mode(tb));

  /* Move cursor to (5,5) - should be absolute */
  process_ansi_string(parser, "\033[6;6H", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5); /* Row 5 (0-based) */
  ASSERT_EQ(col, 5); /* Col 5 (0-based) */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_decom_enabled_relative_positioning) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set scrolling region to rows 2-7 (1-indexed: 3-8) */
  process_ansi_string(parser, "\033[3;8r", tb);
  size_t _st, _sb;
  terminal_buffer_get_scroll_region(tb, &_st, &_sb);
  ASSERT_EQ(_st, 2);
  ASSERT_EQ(_sb, 7);

  /* Enable origin mode */
  process_ansi_string(parser, "\033[?6h", tb);
  ASSERT_TRUE(terminal_buffer_is_origin_mode(tb));

  /* Cursor should move to home (0,0 relative to scroll region) */
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 2); /* scroll_top */
  ASSERT_EQ(col, 0);

  /* Move cursor to (3,3) - should be relative to scroll_top */
  process_ansi_string(parser, "\033[4;4H", tb);
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5); /* scroll_top (2) + 3 = 5 */
  ASSERT_EQ(col, 3);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_decom_toggle_moves_cursor_home) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Move cursor to (5,5) */
  terminal_buffer_set_cursor(tb, 5, 5);

  /* Toggle origin mode on */
  process_ansi_string(parser, "\033[?6h", tb);

  /* Cursor should move to home */
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: DECAWM (Auto-Wrap Mode) Tests - ESC[?7h/l
 * ======================================================================== */

TEST(vt100_decawm_enabled_by_default) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);

  /* Auto-wrap should be enabled by default */
  ASSERT_TRUE(terminal_buffer_is_auto_wrap(tb));

  terminal_buffer_destroy(tb);
}

TEST(vt100_decawm_can_be_toggled) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Disable auto-wrap */
  process_ansi_string(parser, "\033[?7l", tb);
  ASSERT_FALSE(terminal_buffer_is_auto_wrap(tb));

  /* Enable auto-wrap */
  process_ansi_string(parser, "\033[?7h", tb);
  ASSERT_TRUE(terminal_buffer_is_auto_wrap(tb));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: IND (Index) Tests - ESC D
 * ======================================================================== */

TEST(vt100_ind_moves_cursor_down) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (3,5) */
  terminal_buffer_set_cursor(tb, 3, 5);

  /* IND - move down */
  process_ansi_string(parser, "\033D", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 5); /* Column unchanged */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_ind_scrolls_at_bottom_of_region) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set scrolling region 0-4 */
  terminal_buffer_set_scroll_region(tb, 0, 4);

  /* Put a marker at row 0 */
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  cell->codepoint = 'X';

  /* Move to bottom of scroll region */
  terminal_buffer_set_cursor(tb, 4, 0);

  /* IND - should scroll */
  process_ansi_string(parser, "\033D", tb);

  /* Cursor should still be at bottom */
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);

  /* Marker 'X' should have scrolled off (row 0 should be empty) */
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: RI (Reverse Index) Tests - ESC M
 * ======================================================================== */

TEST(vt100_ri_moves_cursor_up) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (5,3) */
  terminal_buffer_set_cursor(tb, 5, 3);

  /* RI - move up */
  process_ansi_string(parser, "\033M", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 3); /* Column unchanged */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_ri_scrolls_down_at_top_of_region) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set scrolling region 2-6 */
  terminal_buffer_set_scroll_region(tb, 2, 6);

  /* Put a marker at row 6 (bottom of region) */
  TermCell *cell = terminal_buffer_get_cell(tb, 6, 0);
  cell->codepoint = 'Y';

  /* Move to top of scroll region */
  terminal_buffer_set_cursor(tb, 2, 0);

  /* RI - should scroll down */
  process_ansi_string(parser, "\033M", tb);

  /* Cursor should still be at top */
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 2);

  /* Row 2 should be empty (new line inserted) */
  cell = terminal_buffer_get_cell(tb, 2, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_ri_stops_at_row_zero_outside_scroll_region) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Move to row 0 */
  terminal_buffer_set_cursor(tb, 0, 5);

  /* RI at row 0 - should not move */
  process_ansi_string(parser, "\033M", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 5);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: DECSC/DECRC (Save/Restore Cursor) Tests - ESC 7/8
 * ======================================================================== */

TEST(vt100_decsc_decrc_save_and_restore_position) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Move to (7,3) */
  terminal_buffer_set_cursor(tb, 7, 3);

  /* Save cursor */
  process_ansi_string(parser, "\0337", tb);

  /* Move to different position */
  terminal_buffer_set_cursor(tb, 2, 8);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 2);
  ASSERT_EQ(col, 8);

  /* Restore cursor */
  process_ansi_string(parser, "\0338", tb);

  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 7);
  ASSERT_EQ(col, 3);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_decsc_decrc_multiple_saves) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Position 1 */
  terminal_buffer_set_cursor(tb, 1, 1);
  process_ansi_string(parser, "\0337", tb);

  /* Position 2 (overwrites saved position) */
  terminal_buffer_set_cursor(tb, 5, 5);
  process_ansi_string(parser, "\0337", tb);

  /* Move elsewhere */
  terminal_buffer_set_cursor(tb, 9, 9);

  /* Restore - should get position 2, not position 1 */
  process_ansi_string(parser, "\0338", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 5);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: NEL (Next Line) Tests - ESC E
 * ======================================================================== */

TEST(vt100_nel_moves_to_next_line_column_zero) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (3,7) */
  terminal_buffer_set_cursor(tb, 3, 7);

  /* NEL */
  process_ansi_string(parser, "\033E", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 0); /* Column reset to 0 */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_nel_scrolls_at_bottom) {
  TerminalBuffer *tb = terminal_buffer_init(5, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Put marker at top */
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  cell->codepoint = 'M';

  /* Move to last row */
  terminal_buffer_set_cursor(tb, 4, 5);

  /* NEL - should scroll */
  process_ansi_string(parser, "\033E", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4); /* Still at bottom */
  ASSERT_EQ(col, 0); /* Column 0 */

  /* Marker should have scrolled off */
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: SGR Attributes Tests
 * ======================================================================== */

TEST(vt100_sgr_bold_sets_attribute) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Enable bold */
  process_ansi_string(parser, "\033[1m", tb);

  /* Check that bold attribute is set */
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_sgr_dim_sets_attribute) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Enable dim */
  process_ansi_string(parser, "\033[2m", tb);

  /* Check that dim attribute is set */
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_DIM);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_sgr_underline_sets_attribute) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Enable underline */
  process_ansi_string(parser, "\033[4m", tb);

  /* Check that underline attribute is set */
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_sgr_reverse_sets_attribute) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Enable reverse */
  process_ansi_string(parser, "\033[7m", tb);

  /* Check that reverse attribute is set */
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_REVERSE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_sgr_multiple_attributes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Enable bold and underline */
  process_ansi_string(parser, "\033[1;4m", tb);

  /* Check both attributes are set */
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_sgr_reset_clears_all_attributes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set multiple attributes */
  process_ansi_string(parser, "\033[1;4;7m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_REVERSE);

  /* Reset */
  process_ansi_string(parser, "\033[0m", tb);

  /* All attributes should be cleared */
  ASSERT_EQ(terminal_buffer_get_current_attr(tb), ATTR_NONE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_sgr_attributes_persist_across_characters) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set bold */
  process_ansi_string(parser, "\033[1mABC", tb);

  /* Check that all characters have bold attribute */
  TermCell *cell;
  for (int i = 0; i < 3; i++) {
    cell = terminal_buffer_get_cell(tb, 0, i);
    ASSERT_TRUE(cell->attributes & ATTR_BOLD);
  }

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: Color Tests (8 colors)
 * ======================================================================== */

TEST(vt100_sgr_foreground_colors) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set red foreground */
  process_ansi_string(parser, "\033[31m", tb);

  /* Default foreground should be changed */
  ASSERT_NE(terminal_buffer_get_default_fg(tb), COLOR_DEFAULT_FG);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_sgr_background_colors) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set blue background */
  process_ansi_string(parser, "\033[44m", tb);

  /* Default background should be changed */
  ASSERT_NE(terminal_buffer_get_default_bg(tb), COLOR_DEFAULT_BG);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_sgr_bright_colors) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set bright red foreground */
  process_ansi_string(parser, "\033[91m", tb);

  /* Should set a bright color */
  ASSERT_NE(terminal_buffer_get_default_fg(tb), COLOR_DEFAULT_FG);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: Integration Tests
 * ======================================================================== */

TEST(vt100_integration_scrolling_with_origin_mode) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set scrolling region */
  process_ansi_string(parser, "\033[3;7r", tb);

  /* Enable origin mode */
  process_ansi_string(parser, "\033[?6h", tb);

  /* Cursor should be at top of scroll region */
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 2); /* scroll_top */

  /* IND should work within scroll region */
  terminal_buffer_set_cursor(tb, 6, 0); /* Bottom of region */
  process_ansi_string(parser, "\033D", tb);

  /* Should still be at bottom (scrolled) */
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 6);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_integration_save_restore_with_attributes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set position and attributes */
  terminal_buffer_set_cursor(tb, 5, 5);
  process_ansi_string(parser, "\033[1;4m", tb); /* Bold + Underline */

  /* Save */
  process_ansi_string(parser, "\0337", tb);

  /* Change position and attributes */
  terminal_buffer_set_cursor(tb, 0, 0);
  process_ansi_string(parser, "\033[0m", tb); /* Reset */

  /* Restore */
  process_ansi_string(parser, "\0338", tb);

  /* Position should be restored */
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 5);

  /* Note: VT100 DECSC/DECRC only saves position, not attributes */
  /* This is correct behavior */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: DA / DSR replies via parser callback
 * ======================================================================== */

TEST(vt100_da_primary_reports_vt100_avo) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  ReplyCap cap;
  reply_cap_clear(&cap);
  ansi_parser_set_reply_callback(parser, capture_reply, &cap);

  process_ansi_string(parser, "\033[c", tb);
  ASSERT_STR_EQ(cap.buf, "\033[?1;2c");

  reply_cap_clear(&cap);
  process_ansi_string(parser, "\033[0c", tb);
  ASSERT_STR_EQ(cap.buf, "\033[?1;2c");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_da_secondary_reports_zeros) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  ReplyCap cap;
  reply_cap_clear(&cap);
  ansi_parser_set_reply_callback(parser, capture_reply, &cap);

  process_ansi_string(parser, "\033[>c", tb);
  ASSERT_STR_EQ(cap.buf, "\033[>0;0;0c");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_dsr_status_ok) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  ReplyCap cap;
  reply_cap_clear(&cap);
  ansi_parser_set_reply_callback(parser, capture_reply, &cap);

  process_ansi_string(parser, "\033[5n", tb);
  ASSERT_STR_EQ(cap.buf, "\033[0n");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_dsr_cpr_one_based) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  ReplyCap cap;
  reply_cap_clear(&cap);
  ansi_parser_set_reply_callback(parser, capture_reply, &cap);

  terminal_buffer_set_cursor(tb, 3, 5);
  process_ansi_string(parser, "\033[6n", tb);
  ASSERT_STR_EQ(cap.buf, "\033[4;6R");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_dsr_cpr_relative_to_origin) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();
  ReplyCap cap;
  reply_cap_clear(&cap);
  ansi_parser_set_reply_callback(parser, capture_reply, &cap);

  /* DECSTBM rows 3-8 (1-based) → scroll_top=2; DECOM; CUP 4;4 → abs (5,3) */
  process_ansi_string(parser, "\033[3;8r", tb);
  process_ansi_string(parser, "\033[?6h", tb);
  process_ansi_string(parser, "\033[4;4H", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 3);

  process_ansi_string(parser, "\033[6n", tb);
  ASSERT_STR_EQ(cap.buf, "\033[4;4R");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_da_without_callback_does_not_crash) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[c", tb);
  process_ansi_string(parser, "\033[0c", tb);
  process_ansi_string(parser, "\033[>c", tb);
  process_ansi_string(parser, "\033[5n", tb);
  process_ansi_string(parser, "\033[6n", tb);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_dec_private_decom_unaffected_by_da_prefix) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();
  ReplyCap cap;
  reply_cap_clear(&cap);
  ansi_parser_set_reply_callback(parser, capture_reply, &cap);

  process_ansi_string(parser, "\033[3;8r", tb);
  process_ansi_string(parser, "\033[?6h", tb);
  ASSERT_TRUE(terminal_buffer_is_origin_mode(tb));

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 2);
  ASSERT_EQ(col, 0);

  process_ansi_string(parser, "\033[c", tb);
  ASSERT_STR_EQ(cap.buf, "\033[?1;2c");
  ASSERT_TRUE(terminal_buffer_is_origin_mode(tb));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT100: IRM (Insert/Replace Mode) Tests - ESC[4h / ESC[4l
 * ======================================================================== */

TEST(vt100_irm_inserts_in_middle_of_line) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "ABCD", tb);
  terminal_buffer_set_cursor(tb, 0, 2);
  process_ansi_string(parser, "\033[4h", tb);
  ASSERT_TRUE(terminal_buffer_is_insert_mode(tb));
  process_ansi_string(parser, "X", tb);

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, (uint32_t)'B');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, (uint32_t)'X');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 3)->codepoint, (uint32_t)'C');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 4)->codepoint, (uint32_t)'D');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_irm_reset_returns_to_replace) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "ABCD", tb);
  process_ansi_string(parser, "\033[4h", tb);
  process_ansi_string(parser, "\033[4l", tb);
  ASSERT_FALSE(terminal_buffer_is_insert_mode(tb));

  terminal_buffer_set_cursor(tb, 0, 2);
  process_ansi_string(parser, "X", tb);

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, (uint32_t)'X');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 3)->codepoint, (uint32_t)'D');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_irm_ignores_private_prefix_and_other_sm) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[?4h", tb);
  ASSERT_FALSE(terminal_buffer_is_insert_mode(tb));

  process_ansi_string(parser, "\033[20h", tb);
  ASSERT_FALSE(terminal_buffer_is_insert_mode(tb));

  process_ansi_string(parser, "\033[20;4h", tb);
  ASSERT_TRUE(terminal_buffer_is_insert_mode(tb));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_irm_last_column_wide_and_combining) {
  TerminalBuffer *tb = terminal_buffer_init(4, 8);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "ABCDEFGH", tb);
  terminal_buffer_set_cursor(tb, 0, 7);
  process_ansi_string(parser, "\033[4hX", tb);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 7)->codepoint, (uint32_t)'X');

  terminal_buffer_clear(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  process_ansi_string(parser, "ABCD", tb);
  terminal_buffer_set_cursor(tb, 0, 2);
  process_ansi_string(parser, "\033[4h\xE4\xB8\x80", tb);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, 0x4E00u);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 4)->codepoint, (uint32_t)'C');

  terminal_buffer_clear(tb);
  terminal_buffer_set_cursor(tb, 0, 0);
  process_ansi_string(parser, "eC", tb);
  terminal_buffer_set_cursor(tb, 0, 1);
  process_ansi_string(parser, "\033[4h\xCC\x81", tb);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, 0x0301u);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, (uint32_t)' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt100_irm_soft_reset_clears_insert_mode) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[4h", tb);
  ASSERT_TRUE(terminal_buffer_is_insert_mode(tb));
  process_ansi_string(parser, "\033[!p", tb);
  ASSERT_FALSE(terminal_buffer_is_insert_mode(tb));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Test Registration
 * ======================================================================== */

void run_vt100_tests(void) {
  /* DECOM (Origin Mode) */
  RUN_TEST(vt100_decom_disabled_absolute_positioning);
  RUN_TEST(vt100_decom_enabled_relative_positioning);
  RUN_TEST(vt100_decom_toggle_moves_cursor_home);

  /* DECAWM (Auto-Wrap Mode) */
  RUN_TEST(vt100_decawm_enabled_by_default);
  RUN_TEST(vt100_decawm_can_be_toggled);

  /* IND (Index) */
  RUN_TEST(vt100_ind_moves_cursor_down);
  RUN_TEST(vt100_ind_scrolls_at_bottom_of_region);

  /* RI (Reverse Index) */
  RUN_TEST(vt100_ri_moves_cursor_up);
  RUN_TEST(vt100_ri_scrolls_down_at_top_of_region);
  RUN_TEST(vt100_ri_stops_at_row_zero_outside_scroll_region);

  /* DECSC/DECRC */
  RUN_TEST(vt100_decsc_decrc_save_and_restore_position);
  RUN_TEST(vt100_decsc_decrc_multiple_saves);

  /* NEL (Next Line) */
  RUN_TEST(vt100_nel_moves_to_next_line_column_zero);
  RUN_TEST(vt100_nel_scrolls_at_bottom);

  /* SGR Attributes */
  RUN_TEST(vt100_sgr_bold_sets_attribute);
  RUN_TEST(vt100_sgr_dim_sets_attribute);
  RUN_TEST(vt100_sgr_underline_sets_attribute);
  RUN_TEST(vt100_sgr_reverse_sets_attribute);
  RUN_TEST(vt100_sgr_multiple_attributes);
  RUN_TEST(vt100_sgr_reset_clears_all_attributes);
  RUN_TEST(vt100_sgr_attributes_persist_across_characters);

  /* Colors */
  RUN_TEST(vt100_sgr_foreground_colors);
  RUN_TEST(vt100_sgr_background_colors);
  RUN_TEST(vt100_sgr_bright_colors);

  /* Integration */
  RUN_TEST(vt100_integration_scrolling_with_origin_mode);
  RUN_TEST(vt100_integration_save_restore_with_attributes);

  /* DA / DSR replies */
  RUN_TEST(vt100_da_primary_reports_vt100_avo);
  RUN_TEST(vt100_da_secondary_reports_zeros);
  RUN_TEST(vt100_dsr_status_ok);
  RUN_TEST(vt100_dsr_cpr_one_based);
  RUN_TEST(vt100_dsr_cpr_relative_to_origin);
  RUN_TEST(vt100_da_without_callback_does_not_crash);
  RUN_TEST(vt100_dec_private_decom_unaffected_by_da_prefix);

  /* IRM (Insert/Replace Mode) */
  RUN_TEST(vt100_irm_inserts_in_middle_of_line);
  RUN_TEST(vt100_irm_reset_returns_to_replace);
  RUN_TEST(vt100_irm_ignores_private_prefix_and_other_sm);
  RUN_TEST(vt100_irm_last_column_wide_and_combining);
  RUN_TEST(vt100_irm_soft_reset_clears_insert_mode);
}
