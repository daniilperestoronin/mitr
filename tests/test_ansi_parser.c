/**
 * test_ansi_parser.c - Tests for ANSI Parser
 */

#include "ansi_parser.h"
#include "test_framework.h"

/* ========================================================================
 * Initialization Tests
 * ======================================================================== */

TEST(ansi_parser_init_creates_parser) {
  AnsiParser *parser = ansi_parser_init();

  ASSERT_NOT_NULL(parser);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);
  ASSERT_EQ(ansi_parser_get_param_count(parser), 0);

  ansi_parser_destroy(parser);
}

TEST(ansi_parser_destroy_handles_null) {
  ansi_parser_destroy(nullptr);
  ASSERT_TRUE(true);
}

/* ========================================================================
 * Basic Text Tests
 * ======================================================================== */

TEST(ansi_parser_passes_through_normal_text) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  const char *text = "Hello";
  ansi_parser_feed(parser, text, strlen(text), tb);

  /* Check characters */
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, 'H');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, 'e');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 2)->codepoint, 'l');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 3)->codepoint, 'l');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 4)->codepoint, 'o');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_handles_newline) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* VT100: LF alone does not return to column 0 — use CR LF */
  const char *text = "Line1\r\nLine2";
  ansi_parser_feed(parser, text, strlen(text), tb);

  uint32_t c00 = terminal_buffer_get_cell(tb, 0, 0)->codepoint;
  uint32_t c10 = terminal_buffer_get_cell(tb, 1, 0)->codepoint;
  uint32_t c14 = terminal_buffer_get_cell(tb, 1, 4)->codepoint;

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);

  ASSERT_EQ(c00, 'L');
  ASSERT_EQ(c10, 'L');
  ASSERT_EQ(c14, '2');
}

TEST(ansi_parser_utf8_rejects_overlong_and_surrogates) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Overlong encoding of '/' (C0 AF) → U+FFFD */
  const unsigned char overlong[] = {0xC0, 0xAF};
  ansi_parser_feed(parser, (const char *)overlong, sizeof(overlong), tb);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, 0xFFFD);

  terminal_buffer_clear(tb);
  terminal_buffer_set_cursor(tb, 0, 0);

  /* UTF-16 surrogate U+D800 encoded as ED A0 80 → U+FFFD */
  const unsigned char surrogate[] = {0xED, 0xA0, 0x80};
  ansi_parser_feed(parser, (const char *)surrogate, sizeof(surrogate), tb);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, 0xFFFD);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_csi_param_saturates_without_overflow) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Huge CUU count must not overflow; cursor stays at top */
  const char *seq = "\x1b[999999999999999999A";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_decstr_requires_intermediate) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  terminal_buffer_set_auto_wrap(tb, false);
  /* CSI p without '!' must not soft-reset */
  ansi_parser_feed(parser, "\x1b[p", 3, tb);
  ASSERT_FALSE(terminal_buffer_is_auto_wrap(tb));

  /* CSI ! p (DECSTR) restores defaults including auto-wrap */
  ansi_parser_feed(parser, "\x1b[!p", 4, tb);
  ASSERT_TRUE(terminal_buffer_is_auto_wrap(tb));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * SGR Color Tests
 * ======================================================================== */

TEST(ansi_parser_sgr_reset) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Set red color */
  terminal_buffer_set_default_fg(tb, COLOR_RED);

  /* Reset with ESC[0m */
  const char *seq = "\x1b[0m";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  ASSERT_EQ(terminal_buffer_get_default_fg(tb), COLOR_DEFAULT_FG);
  ASSERT_EQ(terminal_buffer_get_default_bg(tb), COLOR_DEFAULT_BG);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_sgr_foreground_color) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Red foreground: ESC[31m */
  const char *seq = "\x1b[31m";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  ASSERT_EQ(terminal_buffer_get_default_fg(tb), COLOR_RED);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_sgr_background_color) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Blue background: ESC[44m */
  const char *seq = "\x1b[44m";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  ASSERT_EQ(terminal_buffer_get_default_bg(tb), COLOR_BLUE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_sgr_bright_colors) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Bright red foreground: ESC[91m */
  const char *seq = "\x1b[91m";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  ASSERT_EQ(terminal_buffer_get_default_fg(tb), COLOR_BRIGHT_RED);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_sgr_true_color) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* True color RGB(255,100,50): ESC[38;2;255;100;50m */
  const char *seq = "\x1b[38;2;255;100;50m";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  uint32_t fg = terminal_buffer_get_default_fg(tb);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);

  ASSERT_EQ(fg, RGB(255, 100, 50));
}

TEST(ansi_parser_sgr_multiple_params) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Red on blue: ESC[31;44m */
  const char *seq = "\x1b[31;44m";
  ansi_parser_feed(parser, seq, strlen(seq), tb);

  ASSERT_EQ(terminal_buffer_get_default_fg(tb), COLOR_RED);
  ASSERT_EQ(terminal_buffer_get_default_bg(tb), COLOR_BLUE);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_colored_text) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Red text: ESC[31mHello */
  const char *text = "\x1b[31mHello";
  ansi_parser_feed(parser, text, strlen(text), tb);

  /* Color should be set */
  ASSERT_EQ(terminal_buffer_get_default_fg(tb), COLOR_RED);

  /* Characters should be written */
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, 'H');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, 'e');

  /* Characters should have red color */
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->fg_color, COLOR_RED);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * State Machine Tests
 * ======================================================================== */

TEST(ansi_parser_state_transitions) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Start in GROUND */
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);

  /* Feed ESC */
  ansi_parser_feed(parser, "\x1b", 1, tb);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_ESCAPE);

  /* Feed [ */
  ansi_parser_feed(parser, "[", 1, tb);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_CSI);

  /* Feed final char */
  ansi_parser_feed(parser, "m", 1, tb);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_reset_clears_state) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  /* Put parser in CSI state */
  ansi_parser_feed(parser, "\x1b[", 2, tb);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_CSI);

  /* Reset */
  ansi_parser_reset(parser);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);
  ASSERT_EQ(ansi_parser_get_param_count(parser), 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_backspace) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_clear(tb);

  /* Write text */
  ansi_parser_feed(parser, "Hello", 5, tb);

  /* Cursor should be at position 5 */
  size_t row, col;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(col, 5);

  /* Send backspace */
  ansi_parser_feed(parser, "\b", 1, tb);

  /* Cursor should move back */
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(col, 4);

  /* Character is still there (backspace only moves cursor) */
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 4)->codepoint, 'o');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_ignores_dcs_until_st) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  const char seq[] = "\033P1$r q\033\\Hi";

  ansi_parser_feed(parser, seq, sizeof(seq) - 1, tb);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'H');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 1)->codepoint, (uint32_t)'i');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_ignores_apc_pm_sos) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  const char seq[] = "\033_apc\007\033^pm\033\\\033Xsos\007Z";

  ansi_parser_feed(parser, seq, sizeof(seq) - 1, tb);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'Z');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(ansi_parser_sync_should_present_when_off) {
  ASSERT_TRUE(ansi_parser_sync_should_present(false, 0, 0, 150));
  ASSERT_TRUE(ansi_parser_sync_should_present(false, 1000, 10, 150));
}

TEST(ansi_parser_sync_should_present_waits_for_timeout) {
  ASSERT_FALSE(ansi_parser_sync_should_present(true, 100, 100, 150));
  ASSERT_FALSE(ansi_parser_sync_should_present(true, 249, 100, 150));
  ASSERT_TRUE(ansi_parser_sync_should_present(true, 250, 100, 150));
}

TEST(ansi_parser_sync_should_present_handles_tick_wrap) {
  uint32_t started = UINT32_MAX - 10;
  uint32_t now = 140;
  ASSERT_TRUE(ansi_parser_sync_should_present(true, now, started, 150));
  now = 100;
  ASSERT_FALSE(ansi_parser_sync_should_present(true, now, started, 150));
}

TEST(ansi_parser_sync_started_ms_restarts_when_generation_changes) {
  ASSERT_EQ(ansi_parser_sync_started_ms(1, 0, 1000, 100), 1000);
  ASSERT_EQ(ansi_parser_sync_started_ms(1, 1, 1000, 100), 100);
  ASSERT_EQ(ansi_parser_sync_started_ms(2, 1, 500, 0), 500);
}

TEST(ansi_parser_sync_generation_bumps_twice_on_reenable) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);

  ASSERT_EQ(ansi_parser_sync_generation(nullptr), 0);
  ASSERT_EQ(ansi_parser_sync_generation(parser), 0);

  ansi_parser_feed(parser, "\033[?2026h", 8, tb);
  ASSERT_EQ(ansi_parser_sync_generation(parser), 1);
  ASSERT_TRUE(ansi_parser_is_synchronized(parser));

  const char *seq = "\033[?2026l\033[?2026h\033[?2026h";
  ansi_parser_feed(parser, seq, strlen(seq), tb);
  ASSERT_EQ(ansi_parser_sync_generation(parser), 3);
  ASSERT_TRUE(ansi_parser_is_synchronized(parser));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

void run_ansi_parser_tests(void) {
  printf("\n" TERM_COLOR_YELLOW "=== ANSI Parser Tests ===" TERM_COLOR_RESET
         "\n");
  RUN_TEST(ansi_parser_init_creates_parser);
  RUN_TEST(ansi_parser_destroy_handles_null);
  RUN_TEST(ansi_parser_passes_through_normal_text);
  RUN_TEST(ansi_parser_handles_newline);
  RUN_TEST(ansi_parser_utf8_rejects_overlong_and_surrogates);
  RUN_TEST(ansi_parser_csi_param_saturates_without_overflow);
  RUN_TEST(ansi_parser_decstr_requires_intermediate);
  RUN_TEST(ansi_parser_sgr_reset);
  RUN_TEST(ansi_parser_sgr_foreground_color);
  RUN_TEST(ansi_parser_sgr_background_color);
  RUN_TEST(ansi_parser_sgr_bright_colors);
  RUN_TEST(ansi_parser_sgr_true_color);
  RUN_TEST(ansi_parser_sgr_multiple_params);
  RUN_TEST(ansi_parser_colored_text);
  RUN_TEST(ansi_parser_state_transitions);
  RUN_TEST(ansi_parser_reset_clears_state);
  RUN_TEST(ansi_parser_backspace);
  RUN_TEST(ansi_parser_ignores_dcs_until_st);
  RUN_TEST(ansi_parser_ignores_apc_pm_sos);
  RUN_TEST(ansi_parser_sync_should_present_when_off);
  RUN_TEST(ansi_parser_sync_should_present_waits_for_timeout);
  RUN_TEST(ansi_parser_sync_should_present_handles_tick_wrap);
  RUN_TEST(ansi_parser_sync_started_ms_restarts_when_generation_changes);
  RUN_TEST(ansi_parser_sync_generation_bumps_twice_on_reenable);
}
