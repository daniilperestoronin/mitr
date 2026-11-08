/**
 * test_vt102.c - Tests for VT102 standard implementation
 *
 * Tests for VT102 (1982) terminal standard compliance
 * VT102 extends VT100 with character insert/delete and additional cursor
 * controls
 */

#include "ansi_parser.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include "test_helpers.h"
#include <string.h>

/* ========================================================================
 * VT102: ICH (Insert Character) Tests - ESC[{n}@
 * ======================================================================== */

TEST(vt102_ich_insert_single_character) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABCD" at row 0 */
  process_ansi_string(parser, "ABCD", tb);

  /* Move cursor back to position 2 (after 'B') */
  terminal_buffer_set_cursor(tb, 0, 2);

  /* Insert 1 character (should shift "CD" right) */
  process_ansi_string(parser, "\033[@", tb);

  /* Check: "AB CD" (space inserted, D should be shifted) */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'A');
  cell = terminal_buffer_get_cell(tb, 0, 1);
  ASSERT_EQ(cell->codepoint, 'B');
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, ' '); /* Inserted space */
  cell = terminal_buffer_get_cell(tb, 0, 3);
  ASSERT_EQ(cell->codepoint, 'C');
  cell = terminal_buffer_get_cell(tb, 0, 4);
  ASSERT_EQ(cell->codepoint, 'D');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_ich_insert_multiple_characters) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABCD" */
  process_ansi_string(parser, "ABCD", tb);

  /* Move to position 2 */
  terminal_buffer_set_cursor(tb, 0, 2);

  /* Insert 3 characters */
  process_ansi_string(parser, "\033[3@", tb);

  /* Check: "AB   CD" (3 spaces inserted) */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 3);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 4);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 5);
  ASSERT_EQ(cell->codepoint, 'C');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_ich_at_end_of_line) {
  TerminalBuffer *tb = terminal_buffer_init(10, 5);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABC" */
  process_ansi_string(parser, "ABC", tb);

  /* Move to position 2 */
  terminal_buffer_set_cursor(tb, 0, 2);

  /* Insert 5 characters (more than remaining space) */
  process_ansi_string(parser, "\033[5@", tb);

  /* 'C' should be pushed off, line should be spaces after 'B' */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'A');
  cell = terminal_buffer_get_cell(tb, 0, 1);
  ASSERT_EQ(cell->codepoint, 'B');
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 4);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT102: DCH (Delete Character) Tests - ESC[{n}P
 * ======================================================================== */

TEST(vt102_dch_delete_single_character) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABCDEF" */
  process_ansi_string(parser, "ABCDEF", tb);

  /* Move to position 2 (at 'C') */
  terminal_buffer_set_cursor(tb, 0, 2);

  /* Delete 1 character */
  process_ansi_string(parser, "\033[P", tb);

  /* Check: "ABDEF " ('C' deleted, rest shifted left) */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'A');
  cell = terminal_buffer_get_cell(tb, 0, 1);
  ASSERT_EQ(cell->codepoint, 'B');
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, 'D');
  cell = terminal_buffer_get_cell(tb, 0, 3);
  ASSERT_EQ(cell->codepoint, 'E');
  cell = terminal_buffer_get_cell(tb, 0, 4);
  ASSERT_EQ(cell->codepoint, 'F');
  cell = terminal_buffer_get_cell(tb, 0, 5);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_dch_delete_multiple_characters) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABCDEFGH" */
  process_ansi_string(parser, "ABCDEFGH", tb);

  /* Move to position 2 */
  terminal_buffer_set_cursor(tb, 0, 2);

  /* Delete 3 characters */
  process_ansi_string(parser, "\033[3P", tb);

  /* Check: "ABFGH   " ('CDE' deleted) */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, 'F');
  cell = terminal_buffer_get_cell(tb, 0, 3);
  ASSERT_EQ(cell->codepoint, 'G');
  cell = terminal_buffer_get_cell(tb, 0, 4);
  ASSERT_EQ(cell->codepoint, 'H');
  cell = terminal_buffer_get_cell(tb, 0, 5);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_dch_delete_beyond_end) {
  TerminalBuffer *tb = terminal_buffer_init(10, 5);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABC" */
  process_ansi_string(parser, "ABC", tb);

  /* Move to position 1 */
  terminal_buffer_set_cursor(tb, 0, 1);

  /* Delete 10 characters (more than available) */
  process_ansi_string(parser, "\033[10P", tb);

  /* Check: "A    " (everything after 'A' deleted) */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'A');
  cell = terminal_buffer_get_cell(tb, 0, 1);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT102: CHA (Cursor Horizontal Absolute) Tests - ESC[{n}G
 * ======================================================================== */

TEST(vt102_cha_move_to_column) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (5, 3) */
  terminal_buffer_set_cursor(tb, 5, 3);

  /* Move to column 7 (1-indexed, so column 6 in 0-indexed) */
  process_ansi_string(parser, "\033[7G", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5); /* Row unchanged */
  ASSERT_EQ(col, 6); /* Column 7 (1-indexed) = 6 (0-indexed) */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_cha_move_to_column_1) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (3, 5) */
  terminal_buffer_set_cursor(tb, 3, 5);

  /* Move to column 1 */
  process_ansi_string(parser, "\033[1G", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 3);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_cha_default_to_column_1) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (2, 8) */
  terminal_buffer_set_cursor(tb, 2, 8);

  /* CHA with no parameter (defaults to 1) */
  process_ansi_string(parser, "\033[G", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 2);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT102: VPA (Vertical Position Absolute) Tests - ESC[{n}d
 * ======================================================================== */

TEST(vt102_vpa_move_to_row) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (2, 5) */
  terminal_buffer_set_cursor(tb, 2, 5);

  /* Move to row 7 (1-indexed, so row 6 in 0-indexed) */
  process_ansi_string(parser, "\033[7d", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 6); /* Row 7 (1-indexed) = 6 (0-indexed) */
  ASSERT_EQ(col, 5); /* Column unchanged */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_vpa_move_to_row_1) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (8, 3) */
  terminal_buffer_set_cursor(tb, 8, 3);

  /* Move to row 1 */
  process_ansi_string(parser, "\033[1d", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 3);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_vpa_default_to_row_1) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (7, 4) */
  terminal_buffer_set_cursor(tb, 7, 4);

  /* VPA with no parameter (defaults to 1) */
  process_ansi_string(parser, "\033[d", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 4);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT102: CNL (Cursor Next Line) Tests - ESC[{n}E
 * ======================================================================== */

TEST(vt102_cnl_move_down_one_line) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (3, 5) */
  terminal_buffer_set_cursor(tb, 3, 5);

  /* Move down 1 line */
  process_ansi_string(parser, "\033[E", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 0); /* Column reset to 0 */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_cnl_move_down_multiple_lines) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (2, 7) */
  terminal_buffer_set_cursor(tb, 2, 7);

  /* Move down 3 lines */
  process_ansi_string(parser, "\033[3E", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_cnl_clamp_to_bottom) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (8, 3) */
  terminal_buffer_set_cursor(tb, 8, 3);

  /* Move down 5 lines (would go beyond buffer) */
  process_ansi_string(parser, "\033[5E", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 9); /* Clamped to last row */
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT102: CPL (Cursor Previous Line) Tests - ESC[{n}F
 * ======================================================================== */

TEST(vt102_cpl_move_up_one_line) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (5, 7) */
  terminal_buffer_set_cursor(tb, 5, 7);

  /* Move up 1 line */
  process_ansi_string(parser, "\033[F", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 0); /* Column reset to 0 */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_cpl_move_up_multiple_lines) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (7, 4) */
  terminal_buffer_set_cursor(tb, 7, 4);

  /* Move up 3 lines */
  process_ansi_string(parser, "\033[3F", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_cpl_clamp_to_top) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (2, 6) */
  terminal_buffer_set_cursor(tb, 2, 6);

  /* Move up 5 lines (would go beyond buffer) */
  process_ansi_string(parser, "\033[5F", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0); /* Clamped to first row */
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT102: ECH (Erase Character) Tests - ESC[{n}X
 * ======================================================================== */

TEST(vt102_ech_erase_single_character) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABCDEF" */
  process_ansi_string(parser, "ABCDEF", tb);

  /* Move to position 2 (at 'C') */
  terminal_buffer_set_cursor(tb, 0, 2);

  /* Erase 1 character */
  process_ansi_string(parser, "\033[X", tb);

  /* Check: "AB DEF" ('C' erased to space, cursor doesn't move) */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'A');
  cell = terminal_buffer_get_cell(tb, 0, 1);
  ASSERT_EQ(cell->codepoint, 'B');
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 3);
  ASSERT_EQ(cell->codepoint, 'D');

  /* Cursor should still be at position 2 */
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 2);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_ech_erase_multiple_characters) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABCDEFGH" */
  process_ansi_string(parser, "ABCDEFGH", tb);

  /* Move to position 2 */
  terminal_buffer_set_cursor(tb, 0, 2);

  /* Erase 3 characters */
  process_ansi_string(parser, "\033[3X", tb);

  /* Check: "AB   FGH" ('CDE' erased to spaces) */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 3);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 4);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 5);
  ASSERT_EQ(cell->codepoint, 'F');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_ech_erase_beyond_end) {
  TerminalBuffer *tb = terminal_buffer_init(10, 8);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABCDEF" */
  process_ansi_string(parser, "ABCDEF", tb);

  /* Move to position 4 */
  terminal_buffer_set_cursor(tb, 0, 4);

  /* Erase 10 characters (more than remaining) */
  process_ansi_string(parser, "\033[10X", tb);

  /* Check: "ABCD    " (everything from position 4 to end erased) */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 3);
  ASSERT_EQ(cell->codepoint, 'D');
  cell = terminal_buffer_get_cell(tb, 0, 4);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 5);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 7);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT102: Integration Tests
 * ======================================================================== */

TEST(vt102_integration_ich_and_dch) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Write "ABCD" */
  process_ansi_string(parser, "ABCD", tb);

  /* Move to position 2 */
  terminal_buffer_set_cursor(tb, 0, 2);

  /* Insert 2 characters */
  process_ansi_string(parser, "\033[2@", tb);

  /* Check: "AB  CD" */
  TermCell *cell;
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 4);
  ASSERT_EQ(cell->codepoint, 'C');

  /* Delete 1 character */
  process_ansi_string(parser, "\033[P", tb);

  /* Check: "AB CD" (one space deleted) */
  cell = terminal_buffer_get_cell(tb, 0, 2);
  ASSERT_EQ(cell->codepoint, ' ');
  cell = terminal_buffer_get_cell(tb, 0, 3);
  ASSERT_EQ(cell->codepoint, 'C');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_integration_cha_vpa) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (0, 0) */
  terminal_buffer_set_cursor(tb, 0, 0);

  /* VPA to row 5 */
  process_ansi_string(parser, "\033[5d", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 0);

  /* CHA to column 7 */
  process_ansi_string(parser, "\033[7G", tb);

  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 6);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt102_integration_cnl_cpl) {
  TerminalBuffer *tb = terminal_buffer_init(10, 10);
  AnsiParser *parser = ansi_parser_init();

  /* Start at (5, 5) */
  terminal_buffer_set_cursor(tb, 5, 5);

  /* CNL 2 lines */
  process_ansi_string(parser, "\033[2E", tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 7);
  ASSERT_EQ(col, 0);

  /* Set column back to 5 */
  terminal_buffer_set_cursor(tb, 7, 5);

  /* CPL 3 lines */
  process_ansi_string(parser, "\033[3F", tb);

  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 4);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Test Registration
 * ======================================================================== */

void run_vt102_tests(void) {
  /* ICH (Insert Character) */
  RUN_TEST(vt102_ich_insert_single_character);
  RUN_TEST(vt102_ich_insert_multiple_characters);
  RUN_TEST(vt102_ich_at_end_of_line);

  /* DCH (Delete Character) */
  RUN_TEST(vt102_dch_delete_single_character);
  RUN_TEST(vt102_dch_delete_multiple_characters);
  RUN_TEST(vt102_dch_delete_beyond_end);

  /* CHA (Cursor Horizontal Absolute) */
  RUN_TEST(vt102_cha_move_to_column);
  RUN_TEST(vt102_cha_move_to_column_1);
  RUN_TEST(vt102_cha_default_to_column_1);

  /* VPA (Vertical Position Absolute) */
  RUN_TEST(vt102_vpa_move_to_row);
  RUN_TEST(vt102_vpa_move_to_row_1);
  RUN_TEST(vt102_vpa_default_to_row_1);

  /* CNL (Cursor Next Line) */
  RUN_TEST(vt102_cnl_move_down_one_line);
  RUN_TEST(vt102_cnl_move_down_multiple_lines);
  RUN_TEST(vt102_cnl_clamp_to_bottom);

  /* CPL (Cursor Previous Line) */
  RUN_TEST(vt102_cpl_move_up_one_line);
  RUN_TEST(vt102_cpl_move_up_multiple_lines);
  RUN_TEST(vt102_cpl_clamp_to_top);

  /* ECH (Erase Character) */
  RUN_TEST(vt102_ech_erase_single_character);
  RUN_TEST(vt102_ech_erase_multiple_characters);
  RUN_TEST(vt102_ech_erase_beyond_end);

  /* Integration */
  RUN_TEST(vt102_integration_ich_and_dch);
  RUN_TEST(vt102_integration_cha_vpa);
  RUN_TEST(vt102_integration_cnl_cpl);
}
