/**
 * test_vt320.c - Tests for VT320 standard implementation
 *
 * Tests for VT320 (1987) terminal standard compliance
 */

#include "ansi_parser.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include "test_helpers.h"
#include <string.h>

/* ========================================================================
 * VT320: DECCOLM (132 Column Mode) Tests - ESC[?3h/l
 * ======================================================================== */

TEST(vt320_deccolm_80_columns_default) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  /* Default should be 80 columns */
  ASSERT_EQ(terminal_buffer_get_cols(tb), 80);

  terminal_buffer_destroy(tb);
}

TEST(vt320_deccolm_switch_to_132_columns) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Switch to 132 columns */
  process_ansi_string(parser, "\033[?3h", tb);

  /* Should now be 132 columns */
  ASSERT_EQ(terminal_buffer_get_cols(tb), 132);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_deccolm_switch_back_to_80_columns) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Switch to 132 */
  process_ansi_string(parser, "\033[?3h", tb);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 132);

  /* Switch back to 80 */
  process_ansi_string(parser, "\033[?3l", tb);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 80);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_deccolm_clears_screen_on_switch) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Write some text */
  process_ansi_string(parser, "Hello", tb);
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'H');

  /* Switch to 132 columns - should clear screen */
  process_ansi_string(parser, "\033[?3h", tb);
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT320: DECSCNM (Screen Mode) Tests - ESC[?5h/l
 * ======================================================================== */

TEST(vt320_decscnm_normal_mode_default) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);

  /* Default should be normal mode (not reverse) */
  ASSERT_FALSE(terminal_buffer_is_reverse_screen(tb));

  terminal_buffer_destroy(tb);
}

TEST(vt320_decscnm_enable_reverse_screen) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable reverse screen mode */
  process_ansi_string(parser, "\033[?5h", tb);
  ASSERT_TRUE(terminal_buffer_is_reverse_screen(tb));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_decscnm_disable_reverse_screen) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable then disable */
  process_ansi_string(parser, "\033[?5h", tb);
  ASSERT_TRUE(terminal_buffer_is_reverse_screen(tb));

  process_ansi_string(parser, "\033[?5l", tb);
  ASSERT_FALSE(terminal_buffer_is_reverse_screen(tb));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT320: DECKPAM/DECKPNM (Keypad Modes) Tests - ESC = / ESC >
 * ======================================================================== */

TEST(vt320_deckpnm_numeric_mode_default) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Default should be numeric mode */
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.keypad_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_deckpam_application_mode) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable application mode */
  process_ansi_string(parser, "\033=", tb);
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.keypad_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_deckpnm_numeric_mode) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable application mode first */
  process_ansi_string(parser, "\033=", tb);
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.keypad_application_mode);

  /* Switch back to numeric mode */
  process_ansi_string(parser, "\033>", tb);
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.keypad_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT320: DECCKM (Cursor Keys Mode) Tests - ESC[?1h/l
 * ======================================================================== */

TEST(vt320_decckm_normal_mode_default) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Default should be normal mode */
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.cursor_keys_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_decckm_application_mode) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable application mode */
  process_ansi_string(parser, "\033[?1h", tb);
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.cursor_keys_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_decckm_normal_mode) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable application mode first */
  process_ansi_string(parser, "\033[?1h", tb);
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.cursor_keys_application_mode);

  /* Switch back to normal mode */
  process_ansi_string(parser, "\033[?1l", tb);
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.cursor_keys_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT320: ED mode 3 (Erase Saved Lines) Tests - ESC[3J
 * ======================================================================== */

TEST(vt320_ed_mode3_placeholder) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* ESC[3J should not crash (scrollback not implemented yet) */
  process_ansi_string(parser, "\033[3J", tb);

  /* Just verify it doesn't crash */
  ASSERT_TRUE(true);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT320: SGR 3 (Italic) Tests - ESC[3m / ESC[23m
 * ======================================================================== */

TEST(vt320_sgr_italic_sets_attribute) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[3mA", tb);
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'A');
  ASSERT_TRUE(cell->attributes & ATTR_ITALIC);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_sgr_italic_selective_reset_keeps_bold) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[1;3mB", tb);
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_TRUE(cell->attributes & ATTR_ITALIC);
  ASSERT_TRUE(cell->attributes & ATTR_BOLD);

  /* SGR 23 clears italic only; 22 is bold/dim */
  process_ansi_string(parser, "\033[23mC", tb);
  cell = terminal_buffer_get_cell(tb, 0, 1);
  ASSERT_EQ(cell->codepoint, 'C');
  ASSERT_FALSE(cell->attributes & ATTR_ITALIC);
  ASSERT_TRUE(cell->attributes & ATTR_BOLD);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_sgr_italic_reset_clears_all) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[1;3mX", tb);
  process_ansi_string(parser, "\033[0mY", tb);
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 1);
  ASSERT_EQ(cell->codepoint, 'Y');
  ASSERT_EQ(cell->attributes, ATTR_NONE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT320: SGR 5 (Blink) Tests - ESC[5m
 * ======================================================================== */

TEST(vt320_sgr_blink_sets_attribute) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable blink */
  process_ansi_string(parser, "\033[5m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BLINK);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_sgr_blink_reset) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable blink */
  process_ansi_string(parser, "\033[5m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BLINK);

  /* Reset all attributes */
  process_ansi_string(parser, "\033[0m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_BLINK);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_sgr_blink_selective_reset) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable blink and bold */
  process_ansi_string(parser, "\033[1;5m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BLINK);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);

  /* Reset blink only (SGR 25) */
  process_ansi_string(parser, "\033[25m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_BLINK);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT320: SGR 8 (Concealed) Tests - ESC[8m
 * ======================================================================== */

TEST(vt320_sgr_concealed_sets_attribute) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable concealed */
  process_ansi_string(parser, "\033[8m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_CONCEALED);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_sgr_concealed_reset) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable concealed */
  process_ansi_string(parser, "\033[8m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_CONCEALED);

  /* Reset all attributes */
  process_ansi_string(parser, "\033[0m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_CONCEALED);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_sgr_concealed_selective_reset) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable concealed and bold */
  process_ansi_string(parser, "\033[1;8m", tb);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_CONCEALED);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);

  /* Reset concealed only (SGR 28) */
  process_ansi_string(parser, "\033[28m", tb);
  ASSERT_FALSE(terminal_buffer_get_current_attr(tb) & ATTR_CONCEALED);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT320: SGR 9 (Strikethrough) Tests - ESC[9m / ESC[29m
 * ======================================================================== */

TEST(vt320_sgr_strike_sets_attribute) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[9mA", tb);
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'A');
  ASSERT_TRUE(cell->attributes & ATTR_STRIKE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_sgr_strike_selective_reset_keeps_bold) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[1;9mB", tb);
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_TRUE(cell->attributes & ATTR_STRIKE);
  ASSERT_TRUE(cell->attributes & ATTR_BOLD);

  /* SGR 29 clears strikethrough only */
  process_ansi_string(parser, "\033[29mC", tb);
  cell = terminal_buffer_get_cell(tb, 0, 1);
  ASSERT_EQ(cell->codepoint, 'C');
  ASSERT_FALSE(cell->attributes & ATTR_STRIKE);
  ASSERT_TRUE(cell->attributes & ATTR_BOLD);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_sgr_strike_reset_clears_all) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[1;9mX", tb);
  process_ansi_string(parser, "\033[0mY", tb);
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 1);
  ASSERT_EQ(cell->codepoint, 'Y');
  ASSERT_EQ(cell->attributes, ATTR_NONE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * VT320: Integration Tests
 * ======================================================================== */

TEST(vt320_integration_deccolm_with_content) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Write text in 80-column mode */
  process_ansi_string(parser, "Line 1", tb);

  /* Switch to 132 columns */
  process_ansi_string(parser, "\033[?3h", tb);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 132);

  /* Screen should be cleared */
  TermCell *cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, ' ');

  /* Write new text in 132-column mode */
  process_ansi_string(parser, "Wide Line", tb);
  cell = terminal_buffer_get_cell(tb, 0, 0);
  ASSERT_EQ(cell->codepoint, 'W');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_integration_multiple_attributes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable multiple VT320 attributes */
  process_ansi_string(parser, "\033[1;4;5;8m", tb);

  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BOLD);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_UNDERLINE);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_BLINK);
  ASSERT_TRUE(terminal_buffer_get_current_attr(tb) & ATTR_CONCEALED);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_integration_keypad_and_cursor_modes) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable both keypad and cursor application modes */
  process_ansi_string(parser, "\033=", tb);
  process_ansi_string(parser, "\033[?1h", tb);

  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.keypad_application_mode);
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.cursor_keys_application_mode);

  /* Disable both */
  process_ansi_string(parser, "\033>", tb);
  process_ansi_string(parser, "\033[?1l", tb);

  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.keypad_application_mode);
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.cursor_keys_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(vt320_integration_reverse_screen_with_colors) {
  TerminalBuffer *tb = terminal_buffer_init(10, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Set colors and enable reverse screen */
  process_ansi_string(parser, "\033[31;42m", tb); // Red fg, Green bg
  process_ansi_string(parser, "\033[?5h", tb);    // Reverse screen

  ASSERT_TRUE(terminal_buffer_is_reverse_screen(tb));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Test Registration
 * ======================================================================== */

void run_vt320_tests(void) {
  /* DECCOLM tests */
  RUN_TEST(vt320_deccolm_80_columns_default);
  RUN_TEST(vt320_deccolm_switch_to_132_columns);
  RUN_TEST(vt320_deccolm_switch_back_to_80_columns);
  RUN_TEST(vt320_deccolm_clears_screen_on_switch);

  /* DECSCNM tests */
  RUN_TEST(vt320_decscnm_normal_mode_default);
  RUN_TEST(vt320_decscnm_enable_reverse_screen);
  RUN_TEST(vt320_decscnm_disable_reverse_screen);

  /* DECKPAM/DECKPNM tests */
  RUN_TEST(vt320_deckpnm_numeric_mode_default);
  RUN_TEST(vt320_deckpam_application_mode);
  RUN_TEST(vt320_deckpnm_numeric_mode);

  /* DECCKM tests */
  RUN_TEST(vt320_decckm_normal_mode_default);
  RUN_TEST(vt320_decckm_application_mode);
  RUN_TEST(vt320_decckm_normal_mode);

  /* ED mode 3 tests */
  RUN_TEST(vt320_ed_mode3_placeholder);

  /* SGR 3 (Italic) tests */
  RUN_TEST(vt320_sgr_italic_sets_attribute);
  RUN_TEST(vt320_sgr_italic_selective_reset_keeps_bold);
  RUN_TEST(vt320_sgr_italic_reset_clears_all);

  /* SGR 5 (Blink) tests */
  RUN_TEST(vt320_sgr_blink_sets_attribute);
  RUN_TEST(vt320_sgr_blink_reset);
  RUN_TEST(vt320_sgr_blink_selective_reset);

  /* SGR 8 (Concealed) tests */
  RUN_TEST(vt320_sgr_concealed_sets_attribute);
  RUN_TEST(vt320_sgr_concealed_reset);
  RUN_TEST(vt320_sgr_concealed_selective_reset);

  /* SGR 9 (Strikethrough) tests */
  RUN_TEST(vt320_sgr_strike_sets_attribute);
  RUN_TEST(vt320_sgr_strike_selective_reset_keeps_bold);
  RUN_TEST(vt320_sgr_strike_reset_clears_all);

  /* Integration tests */
  RUN_TEST(vt320_integration_deccolm_with_content);
  RUN_TEST(vt320_integration_multiple_attributes);
  RUN_TEST(vt320_integration_keypad_and_cursor_modes);
  RUN_TEST(vt320_integration_reverse_screen_with_colors);
}
