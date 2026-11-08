/**
 * test_cursor_control.c - Tests for cursor movement and erase sequences
 */

#include "ansi_parser.h"
#include "test_framework.h"

/* ========================================================================
 * Cursor Movement Tests
 * ======================================================================== */

TEST(ansi_cursor_up) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Move cursor to position (5, 10) */
  terminal_buffer_set_cursor(tb, 5, 10);

  /* Send cursor up: ESC[A */
  const char *seq = "\x1b[A";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 10);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_cursor_down) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_cursor(tb, 5, 10);

  /* Send cursor down: ESC[B */
  const char *seq = "\x1b[B";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 6);
  ASSERT_EQ(col, 10);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_cursor_forward) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_cursor(tb, 5, 10);

  /* Send cursor forward: ESC[C */
  const char *seq = "\x1b[C";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 11);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_cursor_backward) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_cursor(tb, 5, 10);

  /* Send cursor backward: ESC[D */
  const char *seq = "\x1b[D";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 9);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_cursor_movement_with_count) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_cursor(tb, 10, 10);

  /* Move up 5 lines: ESC[5A */
  const char *seq = "\x1b[5A";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 10);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_cursor_home) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_cursor(tb, 10, 20);

  /* Cursor home: ESC[H */
  const char *seq = "\x1b[H";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_cursor_position) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Set cursor position to (5, 10): ESC[6;11H (1-indexed) */
  const char *seq = "\x1b[6;11H";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 10);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Erase Sequences Tests
 * ======================================================================== */

TEST(ansi_erase_in_display_entire_screen) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  /* Fill screen with text */
  for (size_t i = 0; i < terminal_buffer_get_rows(tb); i++) {
    for (size_t j = 0; j < terminal_buffer_get_cols(tb); j++) {
      terminal_buffer_set_cursor(tb, i, j);
      terminal_buffer_put_char(tb, 'X');
    }
  }

  /* Clear screen: ESC[2J */
  const char *seq = "\x1b[2J";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  /* Check all cells are spaces */
  for (size_t i = 0; i < 5; i++) {
    for (size_t j = 0; j < 5; j++) {
      ASSERT_EQ(terminal_buffer_get_cell(tb, i, j)->codepoint, ' ');
    }
  }

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_erase_in_line_to_end) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Write text on line 5 */
  terminal_buffer_set_cursor(tb, 5, 0);
  const char *text = "Hello World!";
  ansi_parser_feed(parser, text, strlen(text), tb);

  /* Move cursor to position 5 */
  terminal_buffer_set_cursor(tb, 5, 5);

  /* Erase from cursor to end of line: ESC[K or ESC[0K */
  const char *seq = "\x1b[K";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  /* Check: "Hello" remains, rest is spaces */
  ASSERT_EQ(terminal_buffer_get_cell(tb, 5, 0)->codepoint, 'H');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 5, 4)->codepoint, 'o');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 5, 5)->codepoint, ' ');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 5, 6)->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_erase_in_line_to_beginning) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Write text */
  terminal_buffer_set_cursor(tb, 5, 0);
  const char *text = "Hello World!";
  ansi_parser_feed(parser, text, strlen(text), tb);

  /* Move cursor to position 5 */
  terminal_buffer_set_cursor(tb, 5, 5);

  /* Erase from beginning to cursor: ESC[1K */
  const char *seq = "\x1b[1K";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  /* Check: first 6 chars are spaces, rest remains */
  ASSERT_EQ(terminal_buffer_get_cell(tb, 5, 0)->codepoint, ' ');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 5, 5)->codepoint, ' ');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 5, 6)->codepoint, 'W');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_erase_entire_line) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Write text */
  terminal_buffer_set_cursor(tb, 5, 0);
  const char *text = "Hello World!";
  ansi_parser_feed(parser, text, strlen(text), tb);

  terminal_buffer_set_cursor(tb, 5, 5);

  /* Erase entire line: ESC[2K */
  const char *seq = "\x1b[2K";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  /* Check entire line is spaces */
  for (size_t i = 0; i < 12; i++) {
    ASSERT_EQ(terminal_buffer_get_cell(tb, 5, i)->codepoint, ' ');
  }

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_hpa_move_to_column) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_cursor(tb, 5, 10);

  const char *seq = "\x1b[5`";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 4);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_hpa_default_column_one) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_cursor(tb, 3, 9);

  const char *seq = "\x1b[`";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 3);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_hpa_ignores_decom_for_horizontal_origin) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  terminal_buffer_clear(tb);

  const char *region = "\x1b[3;8r";
  ansi_parser_feed(parser, region, strlen(region), tb);
  const char *decom = "\x1b[?6h";
  ansi_parser_feed(parser, decom, strlen(decom), tb);
  ASSERT_TRUE(terminal_buffer_is_origin_mode(tb));

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 2);
  ASSERT_EQ(col, 0);

  const char *hpa = "\x1b[5`";
  ansi_parser_feed(parser, hpa, strlen(hpa), tb);
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 2);
  ASSERT_EQ(col, 4);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

void run_cursor_control_tests(void) {
  printf("\n" TERM_COLOR_YELLOW "=== Cursor Control Tests ===" TERM_COLOR_RESET
         "\n");
  RUN_TEST(ansi_cursor_up);
  RUN_TEST(ansi_cursor_down);
  RUN_TEST(ansi_cursor_forward);
  RUN_TEST(ansi_cursor_backward);
  RUN_TEST(ansi_cursor_movement_with_count);
  RUN_TEST(ansi_cursor_home);
  RUN_TEST(ansi_cursor_position);
  RUN_TEST(ansi_erase_in_display_entire_screen);
  RUN_TEST(ansi_erase_in_line_to_end);
  RUN_TEST(ansi_erase_in_line_to_beginning);
  RUN_TEST(ansi_erase_entire_line);
  RUN_TEST(ansi_hpa_move_to_column);
  RUN_TEST(ansi_hpa_default_column_one);
  RUN_TEST(ansi_hpa_ignores_decom_for_horizontal_origin);
}
