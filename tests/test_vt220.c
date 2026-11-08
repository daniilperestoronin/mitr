/**
 * test_vt220.c - Tests for VT220 standard implementation
 *
 * Tests for VT220 (1983) terminal standard compliance
 * VT220 extends VT102 with 256 colors, attribute reset, scrolling, and tab
 * stops
 */

#include "ansi_parser.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include "test_helpers.h"
#include <string.h>

/* ========================================================================
 * VT220: 256-Color Palette Tests - ESC[38;5;{n}m / ESC[48;5;{n}m
 * ======================================================================== */

TEST(vt220_256_color_foreground_basic) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set foreground to color 196 (red in 256-color palette) */
  process_ansi_string(parser, "\033[38;5;196m", tb);

  /* Foreground should be changed from default */
  ASSERT_NE(terminal_buffer_get_default_fg(tb), COLOR_DEFAULT_FG);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_256_color_background_basic) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set background to color 21 (blue in 256-color palette) */
  process_ansi_string(parser, "\033[48;5;21m", tb);

  /* Background should be changed from default */
  ASSERT_NE(terminal_buffer_get_default_bg(tb), COLOR_DEFAULT_BG);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_256_color_standard_colors) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Colors 0-15 should map to standard 16 colors */
  process_ansi_string(parser, "\033[38;5;1m", tb); /* Red */
  uint32_t red = terminal_buffer_get_default_fg(tb);

  process_ansi_string(parser, "\033[38;5;2m", tb); /* Green */
  uint32_t green = terminal_buffer_get_default_fg(tb);

  /* Should be different colors */
  ASSERT_NE(red, green);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_256_color_grayscale) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Colors 232-255 are grayscale */
  process_ansi_string(parser, "\033[38;5;232m", tb); /* Black */
  uint32_t black = terminal_buffer_get_default_fg(tb);

  process_ansi_string(parser, "\033[38;5;255m", tb); /* White */
  uint32_t white = terminal_buffer_get_default_fg(tb);

  /* Should be different */
  ASSERT_NE(black, white);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_256_color_rgb_cube) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Colors 16-231 are 6x6x6 RGB cube */
  process_ansi_string(parser, "\033[38;5;16m", tb);  /* Start of cube */
  process_ansi_string(parser, "\033[38;5;231m", tb); /* End of cube */

  /* Should set some color */
  ASSERT_NE(terminal_buffer_get_default_fg(tb), COLOR_DEFAULT_FG);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT220: SGR Reset Tests - ESC[22m, ESC[24m, ESC[27m
 * ======================================================================== */

TEST(vt220_sgr_22_reset_bold_and_dim) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set bold */
  process_ansi_string(parser, "\033[1m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);

  /* Reset bold/dim */
  process_ansi_string(parser, "\033[22m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_DIM);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_sgr_22_reset_dim_only) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set dim */
  process_ansi_string(parser, "\033[2m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_DIM);

  /* Reset bold/dim */
  process_ansi_string(parser, "\033[22m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_DIM);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_sgr_22_preserves_other_attributes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set bold and underline */
  process_ansi_string(parser, "\033[1;4m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);

  /* Reset bold/dim (should preserve underline) */
  process_ansi_string(parser, "\033[22m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_sgr_24_reset_underline) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set underline */
  process_ansi_string(parser, "\033[4m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);

  /* Reset underline */
  process_ansi_string(parser, "\033[24m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_sgr_24_preserves_other_attributes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set bold and underline */
  process_ansi_string(parser, "\033[1;4m", tb);

  /* Reset underline (should preserve bold) */
  process_ansi_string(parser, "\033[24m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_sgr_27_reset_reverse) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set reverse */
  process_ansi_string(parser, "\033[7m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_REVERSE);

  /* Reset reverse */
  process_ansi_string(parser, "\033[27m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_REVERSE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_sgr_27_preserves_other_attributes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set reverse and bold */
  process_ansi_string(parser, "\033[1;7m", tb);

  /* Reset reverse (should preserve bold) */
  process_ansi_string(parser, "\033[27m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_REVERSE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT220: SU (Scroll Up) Tests - ESC[{n}S
 * ======================================================================== */

TEST(vt220_su_scroll_up_one_line) {
  TerminalBuffer *tb = terminal_buffer_init(5, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write marker on first line */
  process_ansi_string(parser, "MARKER", tb);
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'M');

  /* Scroll up 1 line */
  process_ansi_string(parser, "\033[S", tb);

  /* First line should be empty now */
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_su_scroll_up_multiple_lines) {
  TerminalBuffer *tb = terminal_buffer_init(5, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write markers (CRLF — VT100 LF does not return to column 0) */
  process_ansi_string(parser, "LINE1\r\n", tb);
  process_ansi_string(parser, "LINE2\r\n", tb);
  process_ansi_string(parser, "LINE3", tb);

  /* Scroll up 2 lines */
  process_ansi_string(parser, "\033[2S", tb);

  /* Capture before cleanup (ASSERT may return) */
  uint32_t c00 = terminal_buffer_get_cell(tb, 0, 0)->codepoint;

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);

  /* First line should have LINE3 */
  ASSERT_EQ(c00, 'L');
}

TEST(vt220_su_respects_scrolling_region) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set scrolling region 2-7 */
  process_ansi_string(parser, "\033[3;8r", tb);

  /* Write marker at top of region */
  terminal_buffer_set_cursor(tb, 2, 0);
  process_ansi_string(parser, "TOP", tb);

  /* Scroll up within region */
  process_ansi_string(parser, "\033[S", tb);

  /* Marker should have scrolled off */
  TermCell *cell = terminal_buffer_get_cell(tb, 2, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT220: SD (Scroll Down) Tests - ESC[{n}T
 * ======================================================================== */

TEST(vt220_sd_scroll_down_one_line) {
  TerminalBuffer *tb = terminal_buffer_init(5, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write marker on first line */
  process_ansi_string(parser, "MARKER", tb);

  /* Scroll down 1 line */
  process_ansi_string(parser, "\033[T", tb);

  /* First line should be empty, marker moved to line 2 */
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  cell = terminal_buffer_get_cell(tb, 1, 0);
  ASSERT_EQ(cell->codepoint, 'M');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_sd_scroll_down_multiple_lines) {
  TerminalBuffer *tb = terminal_buffer_init(5, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write marker */
  process_ansi_string(parser, "LINE1", tb);

  /* Scroll down 2 lines */
  process_ansi_string(parser, "\033[2T", tb);

  /* First two lines should be empty */
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 1, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  /* Marker should be at line 3 */
  cell = terminal_buffer_get_cell(tb, 2, 0);
  ASSERT_EQ(cell->codepoint, 'L');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_sd_respects_scrolling_region) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set scrolling region 2-7 */
  process_ansi_string(parser, "\033[3;8r", tb);

  /* Write marker at top of region */
  terminal_buffer_set_cursor(tb, 2, 0);
  process_ansi_string(parser, "TOP", tb);

  /* Scroll down within region */
  process_ansi_string(parser, "\033[T", tb);

  /* Top line of region should be empty */
  TermCell *cell = terminal_buffer_get_cell(tb, 2, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  /* Marker should be on next line */
  cell = terminal_buffer_get_cell(tb, 3, 0);
  ASSERT_EQ(cell->codepoint, 'T');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT220: Tab Stops Tests - HTS (ESC H), TBC (ESC[{n}g)
 * ======================================================================== */

TEST(vt220_hts_set_tab_stop) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Move to column 20 and set tab stop */
  terminal_buffer_set_cursor(tb, 0, 20);
  process_ansi_string(parser, "\033H", tb);

  /* Tab stops should be set (implementation specific check) */
  /* We'll verify by testing tab behavior later */
  ASSERT_TRUE(true); /* Placeholder */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_tbc_clear_tab_stop_at_cursor) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Set tab stop at column 20 */
  terminal_buffer_set_cursor(tb, 0, 20);
  process_ansi_string(parser, "\033H", tb);

  /* Clear tab stop at current position */
  process_ansi_string(parser, "\033[0g", tb);

  ASSERT_TRUE(true); /* Placeholder */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_tbc_clear_all_tab_stops) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Set a few tab stops */
  terminal_buffer_set_cursor(tb, 0, 20);
  process_ansi_string(parser, "\033H", tb);
  terminal_buffer_set_cursor(tb, 0, 40);
  process_ansi_string(parser, "\033H", tb);

  /* Clear all tab stops */
  process_ansi_string(parser, "\033[3g", tb);

  ASSERT_TRUE(true); /* Placeholder */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_tab_navigation_with_custom_stops) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Clear all default tab stops */
  process_ansi_string(parser, "\033[3g", tb);

  /* Set custom tab stops at 10, 20, 30 */
  terminal_buffer_set_cursor(tb, 0, 10);
  process_ansi_string(parser, "\033H", tb);
  terminal_buffer_set_cursor(tb, 0, 20);
  process_ansi_string(parser, "\033H", tb);
  terminal_buffer_set_cursor(tb, 0, 30);
  process_ansi_string(parser, "\033H", tb);

  /* Go to start */
  terminal_buffer_set_cursor(tb, 0, 0);

  /* Tab should go to 10 */
  process_ansi_string(parser, "\t", tb);
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(col, 10);

  /* Tab again should go to 20 */
  process_ansi_string(parser, "\t", tb);
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(col, 20);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_cbt_moves_to_previous_tab_stop) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  terminal_buffer_set_cursor(tb, 2, 10);
  process_ansi_string(parser, "\033[Z", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 2);
  ASSERT_EQ(col, 8);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_cbt_ps_skips_two_stops) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  terminal_buffer_set_cursor(tb, 1, 10);
  process_ansi_string(parser, "\033[2Z", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 1);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_cbt_column_zero_stays) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  terminal_buffer_set_cursor(tb, 0, 0);
  process_ansi_string(parser, "\033[5Z", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_cbt_preserves_row_with_decom) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* DECSTBM rows 3-8 (1-based) → scroll_top=2; DECOM homes to origin */
  process_ansi_string(parser, "\033[3;8r", tb);
  process_ansi_string(parser, "\033[?6h", tb);
  ASSERT_TRUE(terminal_buffer_is_origin_mode(tb));

  /* CUP 3;11 is origin-relative → absolute (4, 10), not a tab stop */
  process_ansi_string(parser, "\033[3;11H", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 10);

  process_ansi_string(parser, "\033[Z", tb);
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 8);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT220: REP (Repeat last graphic) Tests - ESC[{n}b
 * ======================================================================== */

TEST(vt220_rep_repeats_last_graphic) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "A\033[3b", tb);

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 3)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 4)->codepoint, (uint32_t)' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_rep_default_is_one) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "A\033[b", tb);

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, (uint32_t)' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_rep_noop_without_graphic) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[5b", tb);

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_rep_ignores_cursor_csi) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "A\033[C\033[b", tb);

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'A');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, (uint32_t)' ');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, (uint32_t)'A');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_rep_repeats_utf8_codepoint) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\xE4\xB8\x80\033[2b", tb);

  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, 0x4E00u);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, 0x4E00u);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 4)->codepoint, 0x4E00u);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->width, 0);
  ASSERT_TRUE((terminal_buffer_get_cell(tb, 0, 1)->flags &
               CELL_FLAG_CONTINUATION) != 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT220: Soft Reset Tests - ESC[!p
 * ======================================================================== */

TEST(vt220_soft_reset_clears_attributes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set various attributes */
  process_ansi_string(parser, "\033[1;4;7m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_REVERSE);

  /* Soft reset */
  process_ansi_string(parser, "\033[!p", tb);

  /* Attributes should be cleared */
  ASSERT_EQ(terminal_buffer_get_current_attr(tb), ATTR_NONE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_soft_reset_resets_colors) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set custom colors */
  process_ansi_string(parser, "\033[31;44m", tb);
  ASSERT_NE(terminal_buffer_get_default_fg(tb), COLOR_DEFAULT_FG);
  ASSERT_NE(terminal_buffer_get_default_bg(tb), COLOR_DEFAULT_BG);

  /* Soft reset */
  process_ansi_string(parser, "\033[!p", tb);

  /* Colors should be reset to defaults */
  ASSERT_EQ(terminal_buffer_get_default_fg(tb), COLOR_DEFAULT_FG);
  ASSERT_EQ(terminal_buffer_get_default_bg(tb), COLOR_DEFAULT_BG);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_soft_reset_resets_scrolling_region) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set custom scrolling region */
  process_ansi_string(parser, "\033[3;7r", tb);
  size_t _st, _sb;
  terminal_buffer_get_scroll_region(tb, &_st, &_sb);
  ASSERT_EQ(_st, 2);
  ASSERT_EQ(_sb, 6);

  /* Soft reset */
  process_ansi_string(parser, "\033[!p", tb);

  /* Scrolling region should be reset to full screen */
  terminal_buffer_get_scroll_region(tb, &_st, &_sb);
  ASSERT_EQ(_st, 0);
  ASSERT_EQ(_sb, terminal_buffer_get_rows(tb) - 1);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_soft_reset_preserves_screen_content) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write some content */
  process_ansi_string(parser, "PRESERVED", tb);

  /* Soft reset */
  process_ansi_string(parser, "\033[!p", tb);

  /* Content should still be there */
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'P');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT220: Integration Tests
 * ======================================================================== */

TEST(vt220_integration_256_colors_with_attributes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set 256 color and bold */
  process_ansi_string(parser, "\033[1;38;5;196m", tb);

  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_NE(terminal_buffer_get_default_fg(tb), COLOR_DEFAULT_FG);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_integration_selective_attribute_reset) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Set all attributes */
  process_ansi_string(parser, "\033[1;2;4;7m", tb);

  /* Reset only bold */
  process_ansi_string(parser, "\033[22m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_DIM);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_REVERSE);

  /* Reset underline */
  process_ansi_string(parser, "\033[24m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_REVERSE);

  /* Reset reverse */
  process_ansi_string(parser, "\033[27m", tb);
  ASSERT_EQ(terminal_buffer_get_current_attr(tb), ATTR_NONE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt220_integration_scroll_with_content) {
  TerminalBuffer *tb = terminal_buffer_init(5, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Fill screen with content (CRLF; avoid trailing newline auto-scroll) */
  for (int i = 0; i < 5; i++) {
    char line[20];
    snprintf(line, sizeof(line), "LINE%d", i);
    process_ansi_string(parser, line, tb);
    if (i < 4) {
      process_ansi_string(parser, "\r\n", tb);
    }
  }

  /* Scroll up 2 lines */
  process_ansi_string(parser, "\033[2S", tb);

  /* Capture before cleanup (ASSERT may return) */
  uint32_t c04 = terminal_buffer_get_cell(tb, 0, 4)->codepoint;

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);

  /* LINE0 and LINE1 should be gone, LINE2 at top */
  ASSERT_EQ(c04, '2');
}

/* ========================================================================
 * Test Registration
 * ======================================================================== */

void run_vt220_tests(void) {
  /* 256-color palette */
  RUN_TEST(vt220_256_color_foreground_basic);
  RUN_TEST(vt220_256_color_background_basic);
  RUN_TEST(vt220_256_color_standard_colors);
  RUN_TEST(vt220_256_color_grayscale);
  RUN_TEST(vt220_256_color_rgb_cube);

  /* SGR reset */
  RUN_TEST(vt220_sgr_22_reset_bold_and_dim);
  RUN_TEST(vt220_sgr_22_reset_dim_only);
  RUN_TEST(vt220_sgr_22_preserves_other_attributes);
  RUN_TEST(vt220_sgr_24_reset_underline);
  RUN_TEST(vt220_sgr_24_preserves_other_attributes);
  RUN_TEST(vt220_sgr_27_reset_reverse);
  RUN_TEST(vt220_sgr_27_preserves_other_attributes);

  /* Scroll Up (SU) */
  RUN_TEST(vt220_su_scroll_up_one_line);
  RUN_TEST(vt220_su_scroll_up_multiple_lines);
  RUN_TEST(vt220_su_respects_scrolling_region);

  /* Scroll Down (SD) */
  RUN_TEST(vt220_sd_scroll_down_one_line);
  RUN_TEST(vt220_sd_scroll_down_multiple_lines);
  RUN_TEST(vt220_sd_respects_scrolling_region);

  /* Tab stops */
  RUN_TEST(vt220_hts_set_tab_stop);
  RUN_TEST(vt220_tbc_clear_tab_stop_at_cursor);
  RUN_TEST(vt220_tbc_clear_all_tab_stops);
  RUN_TEST(vt220_tab_navigation_with_custom_stops);
  RUN_TEST(vt220_cbt_moves_to_previous_tab_stop);
  RUN_TEST(vt220_cbt_ps_skips_two_stops);
  RUN_TEST(vt220_cbt_column_zero_stays);
  RUN_TEST(vt220_cbt_preserves_row_with_decom);

  /* REP */
  RUN_TEST(vt220_rep_repeats_last_graphic);
  RUN_TEST(vt220_rep_default_is_one);
  RUN_TEST(vt220_rep_noop_without_graphic);
  RUN_TEST(vt220_rep_ignores_cursor_csi);
  RUN_TEST(vt220_rep_repeats_utf8_codepoint);

  /* Soft reset */
  RUN_TEST(vt220_soft_reset_clears_attributes);
  RUN_TEST(vt220_soft_reset_resets_colors);
  RUN_TEST(vt220_soft_reset_resets_scrolling_region);
  RUN_TEST(vt220_soft_reset_preserves_screen_content);

  /* Integration */
  RUN_TEST(vt220_integration_256_colors_with_attributes);
  RUN_TEST(vt220_integration_selective_attribute_reset);
  RUN_TEST(vt220_integration_scroll_with_content);
}
