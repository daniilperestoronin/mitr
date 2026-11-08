/**
 * test_unicode.c - Golden tests for terminal-aware Unicode cell model
 */

#include "ansi_parser.h"
#include "mouse.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include "test_helpers.h"
#include "unicode.h"
#include <string.h>

/* ========================================================================
 * unicode_wcwidth / unicode_is_combining
 * ======================================================================== */

TEST(unicode_wcwidth_ascii_narrow) {
  ASSERT_EQ(unicode_wcwidth('A'), 1);
  ASSERT_EQ(unicode_wcwidth('z'), 1);
  ASSERT_EQ(unicode_wcwidth('0'), 1);
  ASSERT_EQ(unicode_wcwidth(' '), 1);
}

TEST(unicode_wcwidth_cjk_wide) {
  ASSERT_EQ(unicode_wcwidth(0x4E00), 2); /* 一 */
  ASSERT_EQ(unicode_wcwidth(0x9FFF), 2);
  ASSERT_EQ(unicode_wcwidth(0x3042), 2); /* あ hiragana */
  ASSERT_EQ(unicode_wcwidth(0xAC00), 2); /* 가 hangul */
}

TEST(unicode_wcwidth_emoji_wide) {
  ASSERT_EQ(unicode_wcwidth(0x1F600), 2); /* 😀 */
  ASSERT_EQ(unicode_wcwidth(0x1F1E6), 2); /* regional indicator */
}

TEST(unicode_wcwidth_combining_zero) {
  ASSERT_EQ(unicode_wcwidth(0x0301), 0); /* combining acute */
  ASSERT_TRUE(unicode_is_combining(0x0301));
  ASSERT_TRUE(unicode_is_combining(0x0308)); /* combining diaeresis */
  ASSERT_FALSE(unicode_is_combining('A'));
}

TEST(unicode_wcwidth_invalid) {
  ASSERT_EQ(unicode_wcwidth(0x00), -1);
  ASSERT_EQ(unicode_wcwidth(0x1F), -1);
  ASSERT_EQ(unicode_wcwidth(0xD800), -1);
  ASSERT_EQ(unicode_wcwidth(0xDFFF), -1);
}

TEST(unicode_wcwidth_replacement_narrow) {
  ASSERT_EQ(unicode_wcwidth(0xFFFD), 1);
}

TEST(unicode_range_table_boundaries) {
  /* Combining Marks block 0x0300-0x036F */
  ASSERT_FALSE(unicode_is_combining(0x02FF));
  ASSERT_TRUE(unicode_is_combining(0x0300));
  ASSERT_TRUE(unicode_is_combining(0x036F));
  ASSERT_FALSE(unicode_is_combining(0x0370));
  ASSERT_EQ(unicode_wcwidth(0x02FF), 1);
  ASSERT_EQ(unicode_wcwidth(0x0300), 0);
  ASSERT_EQ(unicode_wcwidth(0x036F), 0);
  ASSERT_EQ(unicode_wcwidth(0x0370), 1);

  /* Gap between 0x4DBF (wide) and CJK 0x4E00 */
  ASSERT_EQ(unicode_wcwidth(0x4DBF), 2);
  ASSERT_EQ(unicode_wcwidth(0x4DC0), 1);
  ASSERT_EQ(unicode_wcwidth(0x4DFF), 1);
  ASSERT_EQ(unicode_wcwidth(0x4E00), 2);
  ASSERT_EQ(unicode_wcwidth(0x9FFF), 2);
  ASSERT_EQ(unicode_wcwidth(0xA000), 2);

  /* Regional indicators sit before the emoji block */
  ASSERT_EQ(unicode_wcwidth(0x1F1E5), 1);
  ASSERT_EQ(unicode_wcwidth(0x1F1E6), 2);
  ASSERT_EQ(unicode_wcwidth(0x1F1FF), 2);
  ASSERT_EQ(unicode_wcwidth(0x1F200), 1);
  ASSERT_EQ(unicode_wcwidth(0x1F300), 2);
}

/* ========================================================================
 * terminal_buffer_put_codepoint — narrow, wide, combining
 * ======================================================================== */

TEST(terminal_buffer_put_codepoint_wide_cjk) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_codepoint(tb, 0x4E00); /* 一 */

  const TermCell *base = terminal_buffer_get_cell_const(tb, 0, 0);
  const TermCell *cont = terminal_buffer_get_cell_const(tb, 0, 1);
  ASSERT_NOT_NULL(base);
  ASSERT_NOT_NULL(cont);
  ASSERT_EQ(base->codepoint, 0x4E00u);
  ASSERT_EQ(base->width, 2);
  ASSERT_EQ(base->flags, 0u);
  ASSERT_TRUE((cont->flags & CELL_FLAG_CONTINUATION) != 0);
  ASSERT_EQ(cont->width, 0u);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0u);
  ASSERT_EQ(col, 2u);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_put_codepoint_combining) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_codepoint(tb, 'e');
  terminal_buffer_put_codepoint(tb, 0x0301); /* combining acute */

  const TermCell *base = terminal_buffer_get_cell_const(tb, 0, 0);
  const TermCell *comb = terminal_buffer_get_cell_const(tb, 0, 1);
  ASSERT_NOT_NULL(base);
  ASSERT_NOT_NULL(comb);
  ASSERT_EQ(base->codepoint, (uint32_t)'e');
  ASSERT_EQ(base->width, 1u);
  ASSERT_EQ(comb->codepoint, 0x0301u);
  ASSERT_EQ(comb->width, 0u);
  ASSERT_EQ(comb->flags, 0u);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0u);
  ASSERT_EQ(col, 1u);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_put_codepoint_emoji_pair) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_codepoint(tb, 0x1F600); /* 😀 wide */

  const TermCell *base = terminal_buffer_get_cell_const(tb, 0, 0);
  const TermCell *cont = terminal_buffer_get_cell_const(tb, 0, 1);
  ASSERT_NOT_NULL(base);
  ASSERT_EQ(base->codepoint, 0x1F600u);
  ASSERT_EQ(base->width, 2u);
  ASSERT_TRUE((cont->flags & CELL_FLAG_CONTINUATION) != 0);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(col, 2u);

  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_set_cursor_snaps_off_continuation) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_codepoint(tb, 0x4E00);

  terminal_buffer_set_cursor(tb, 0, 1);
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(col, 0u);

  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Mouse selection — wide char treated as one unit
 * ======================================================================== */

TEST(mouse_selection_skips_continuation_cells) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  MouseManager *mm = mouse_manager_init();
  ASSERT_NOT_NULL(tb);
  ASSERT_NOT_NULL(mm);

  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_codepoint(tb, 0x4E00);
  terminal_buffer_put_codepoint(tb, 'X');

  mouse_manager_start_selection(mm, 0, 0);
  mouse_manager_update_selection(mm, 0, 2);
  mouse_manager_finish_selection(mm);

  char *text = mouse_manager_get_selected_text(mm, tb);
  ASSERT_NOT_NULL(text);
  ASSERT_EQ((int)strlen(text), 4); /* 一 (3 UTF-8) + X (1) */
  ASSERT_EQ((unsigned char)text[0], 0xE4u);
  ASSERT_EQ((unsigned char)text[3], (unsigned char)'X');

  free(text);
  mouse_manager_destroy(mm);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Malformed UTF-8 via ANSI parser → U+FFFD
 * ======================================================================== */

TEST(ansi_parser_malformed_utf8_replacement_cell) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  AnsiParser *parser = ansi_parser_init();
  ASSERT_NOT_NULL(tb);
  ASSERT_NOT_NULL(parser);

  process_ansi_string(parser, "\xC0\x80", tb); /* overlong NUL */

  const TermCell *cell = terminal_buffer_get_cell_const(tb, 0, 0);
  ASSERT_NOT_NULL(cell);
  ASSERT_EQ(cell->codepoint, 0xFFFDu);
  ASSERT_EQ(cell->width, 1u);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_init_cell_default_width) {
  TerminalBuffer *tb = terminal_buffer_init(2, 5);
  const TermCell *cell = terminal_buffer_get_cell_const(tb, 0, 0);
  ASSERT_NOT_NULL(cell);
  ASSERT_EQ(cell->width, 1u);
  ASSERT_EQ(cell->flags, 0u);
  ASSERT_EQ(cell->codepoint, (uint32_t)' ');
  terminal_buffer_destroy(tb);
}

TEST(terminal_buffer_snap_column_off_continuation) {
  TerminalBuffer *tb = terminal_buffer_init(1, 3);
  ASSERT_NOT_NULL(tb);

  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_codepoint(tb, 0x4E00);

  ASSERT_EQ(terminal_buffer_snap_column(tb, 0, 1), 0u);
  ASSERT_EQ(terminal_buffer_snap_column(tb, 0, 0), 0u);
  ASSERT_EQ(terminal_buffer_snap_column(tb, 0, 2), 2u);

  terminal_buffer_destroy(tb);
}

void run_unicode_tests(void) {
  printf("\n" TERM_COLOR_YELLOW
         "=== Unicode Cell Model Tests ===" TERM_COLOR_RESET "\n");
  RUN_TEST(unicode_wcwidth_ascii_narrow);
  RUN_TEST(unicode_wcwidth_cjk_wide);
  RUN_TEST(unicode_wcwidth_emoji_wide);
  RUN_TEST(unicode_wcwidth_combining_zero);
  RUN_TEST(unicode_wcwidth_invalid);
  RUN_TEST(unicode_wcwidth_replacement_narrow);
  RUN_TEST(unicode_range_table_boundaries);
  RUN_TEST(terminal_buffer_put_codepoint_wide_cjk);
  RUN_TEST(terminal_buffer_put_codepoint_combining);
  RUN_TEST(terminal_buffer_put_codepoint_emoji_pair);
  RUN_TEST(terminal_buffer_set_cursor_snaps_off_continuation);
  RUN_TEST(mouse_selection_skips_continuation_cells);
  RUN_TEST(ansi_parser_malformed_utf8_replacement_cell);
  RUN_TEST(terminal_buffer_init_cell_default_width);
  RUN_TEST(terminal_buffer_snap_column_off_continuation);
}
