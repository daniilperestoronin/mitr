/**
 * test_xterm.c - Tests for XTERM standard features
 *
 * XTERM standard extends VT320 with:
 * - OSC sequences (window title, icon name, clipboard)
 * - Mouse reporting modes
 * - Bracketed paste mode
 * - Application cursor keys (alias for DECCKM)
 */

#include "ansi_parser.h"
#include "mouse.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include "test_helpers.h"
#include <string.h>

static char g_captured_title[256];
static char g_captured_reply[64];
static size_t g_captured_reply_len;

static void capture_reply_bytes(void *userdata, const char *data, size_t len) {
  (void)userdata;
  if (!data) {
    g_captured_reply[0] = '\0';
    g_captured_reply_len = 0;
    return;
  }
  size_t n = len;
  if (n >= sizeof(g_captured_reply)) {
    n = sizeof(g_captured_reply) - 1;
  }
  memcpy(g_captured_reply, data, n);
  g_captured_reply[n] = '\0';
  g_captured_reply_len = n;
}

static void capture_title(void *userdata, const char *title) {
  (void)userdata;
  if (!title) {
    g_captured_title[0] = '\0';
    return;
  }
  size_t n = strlen(title);
  if (n >= sizeof(g_captured_title)) {
    n = sizeof(g_captured_title) - 1;
  }
  memcpy(g_captured_title, title, n);
  g_captured_title[n] = '\0';
}

static char g_clip_text[256];
static size_t g_clip_len;
static int g_clip_calls;

static void clip_cap_clear(void) {
  g_clip_text[0] = '\0';
  g_clip_len = 0;
  g_clip_calls = 0;
}

static void capture_clipboard(void *userdata, const char *text, size_t len) {
  (void)userdata;
  g_clip_calls++;
  if (!text) {
    g_clip_text[0] = '\0';
    g_clip_len = 0;
    return;
  }
  size_t n = len;
  if (n >= sizeof(g_clip_text)) {
    n = sizeof(g_clip_text) - 1;
  }
  memcpy(g_clip_text, text, n);
  g_clip_text[n] = '\0';
  g_clip_len = len;
}

/* ========================================================================
 * XTERM: OSC Sequences - Window Title Tests
 * ======================================================================== */

TEST(xterm_osc_0_set_window_title) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  g_captured_title[0] = '\0';
  ansi_parser_set_title_callback(parser, capture_title, nullptr);

  const char *sequence = "\033]0;My Terminal Window\007";
  process_ansi_string(parser, sequence, tb);

  ASSERT_STR_EQ(g_captured_title, "My Terminal Window");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_2_set_window_title) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  g_captured_title[0] = '\0';
  ansi_parser_set_title_callback(parser, capture_title, nullptr);

  const char *sequence = "\033]2;Test Window\007";
  process_ansi_string(parser, sequence, tb);

  ASSERT_STR_EQ(g_captured_title, "Test Window");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_1_set_icon_name) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  g_captured_title[0] = '\0';
  ansi_parser_set_title_callback(parser, capture_title, nullptr);

  const char *sequence = "\033]1;MyIcon\007";
  process_ansi_string(parser, sequence, tb);

  ASSERT_STR_EQ(g_captured_title, "");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_empty_title) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  memcpy(g_captured_title, "stale", 6);
  ansi_parser_set_title_callback(parser, capture_title, nullptr);

  const char *sequence = "\033]0;\007";
  process_ansi_string(parser, sequence, tb);

  ASSERT_STR_EQ(g_captured_title, "");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_long_title) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* XTERM: Long title (should be truncated or handled) */
  char long_title[2000];
  strcpy(long_title, "\033]0;");
  for (int i = 0; i < 200; i++) {
    strcat(long_title, "Long");
  }
  strcat(long_title, "\007");

  process_ansi_string(parser, long_title, tb);

  ASSERT_TRUE(true); /* Should not crash */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_title_with_semicolons) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* XTERM: Title with semicolons */
  const char *sequence = "\033]0;Test;With;Semicolons\007";
  process_ansi_string(parser, sequence, tb);

  ASSERT_TRUE(true);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_title_with_special_chars) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* XTERM: Title with special characters */
  const char *sequence = "\033]0;Test: [Status] | 50% - Complete!\007";
  process_ansi_string(parser, sequence, tb);

  ASSERT_TRUE(true);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_rejects_non_numeric_command) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033]abc;title\007Hello", tb);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);
  ASSERT_EQ(terminal_buffer_get_cell_const(tb, 0, 0)->codepoint, (uint32_t)'H');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_rejects_overflow_command) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033]99999999999999999999;x\007OK", tb);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);
  ASSERT_EQ(terminal_buffer_get_cell_const(tb, 0, 0)->codepoint, (uint32_t)'O');

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_st_terminator) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* XTERM: OSC can also be terminated by ESC \ (ST - String Terminator) */
  const char *sequence = "\033]0;Test Window\033\\";
  process_ansi_string(parser, sequence, tb);

  ASSERT_TRUE(true);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * XTERM: OSC 52 - Clipboard write (queries ignored)
 * ======================================================================== */

TEST(xterm_osc_52_clipboard_write) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  clip_cap_clear();
  ansi_parser_set_clipboard_callback(parser, capture_clipboard, nullptr);

  process_ansi_string(parser, "\033]52;c;aGVsbG8=\007", tb);

  ASSERT_EQ(g_clip_calls, 1);
  ASSERT_EQ(g_clip_len, 5);
  ASSERT_STR_EQ(g_clip_text, "hello");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_52_st_terminator) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  clip_cap_clear();
  ansi_parser_set_clipboard_callback(parser, capture_clipboard, nullptr);

  process_ansi_string(parser, "\033]52;c;aGVsbG8=\033\\", tb);

  ASSERT_EQ(g_clip_calls, 1);
  ASSERT_STR_EQ(g_clip_text, "hello");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_52_query_ignored) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  clip_cap_clear();
  ansi_parser_set_clipboard_callback(parser, capture_clipboard, nullptr);

  process_ansi_string(parser, "\033]52;c;?\007", tb);

  ASSERT_EQ(g_clip_calls, 0);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_52_invalid_base64) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  clip_cap_clear();
  ansi_parser_set_clipboard_callback(parser, capture_clipboard, nullptr);

  process_ansi_string(parser, "\033]52;c;not-valid!!!\007", tb);

  ASSERT_EQ(g_clip_calls, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_52_empty_pd) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  clip_cap_clear();
  ansi_parser_set_clipboard_callback(parser, capture_clipboard, nullptr);

  process_ansi_string(parser, "\033]52;c;\007", tb);

  ASSERT_EQ(g_clip_calls, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_52_overflow_dropped) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  const char *prefix = "\033]52;c;";
  size_t prefix_n = strlen(prefix);
  size_t pd_n = 70000;
  char *seq;

  clip_cap_clear();
  ansi_parser_set_clipboard_callback(parser, capture_clipboard, nullptr);

  seq = checked_malloc(prefix_n + pd_n + 2);
  ASSERT_NOT_NULL(seq);
  memcpy(seq, prefix, prefix_n);
  memset(seq + prefix_n, 'A', pd_n);
  seq[prefix_n + pd_n] = '\007';
  seq[prefix_n + pd_n + 1] = '\0';
  process_ansi_string(parser, seq, tb);
  free(seq);

  ASSERT_EQ(g_clip_calls, 0);
  ASSERT_EQ(ansi_parser_get_state(parser), PARSER_STATE_GROUND);

  process_ansi_string(parser, "\033]52;c;aGVsbG8=\007", tb);
  ASSERT_EQ(g_clip_calls, 1);
  ASSERT_STR_EQ(g_clip_text, "hello");

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * XTERM: OSC 8 - Hyperlinks
 * ======================================================================== */

TEST(xterm_osc_8_hyperlink_applies_to_cells) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033]8;;https://example.com\033\\", tb);
  process_ansi_string(parser, "AB", tb);
  process_ansi_string(parser, "\033]8;;\033\\", tb);
  process_ansi_string(parser, "C", tb);

  ASSERT_STR_EQ(terminal_buffer_get_hyperlink_at(tb, 0, 0),
                "https://example.com");
  ASSERT_STR_EQ(terminal_buffer_get_hyperlink_at(tb, 0, 1),
                "https://example.com");
  ASSERT_NULL(terminal_buffer_get_hyperlink_at(tb, 0, 2));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_osc_8_empty_clears) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033]8;;https://example.com\007", tb);
  process_ansi_string(parser, "A", tb);
  process_ansi_string(parser, "\033]8;;\007", tb);
  process_ansi_string(parser, "B", tb);

  ASSERT_STR_EQ(terminal_buffer_get_hyperlink_at(tb, 0, 0),
                "https://example.com");
  ASSERT_NULL(terminal_buffer_get_hyperlink_at(tb, 0, 1));

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * XTERM: Bracketed Paste Mode Tests
 * ======================================================================== */

TEST(xterm_bracketed_paste_mode_enable) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* XTERM: ESC[?2004h - Enable bracketed paste mode */
  const char *sequence = "\033[?2004h";
  process_ansi_string(parser, sequence, tb);

  /* Check that parser has bracketed paste enabled */
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.bracketed_paste_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_bracketed_paste_mode_disable) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable first */
  process_ansi_string(parser, "\033[?2004h", tb);
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.bracketed_paste_mode);

  /* XTERM: ESC[?2004l - Disable bracketed paste mode */
  const char *sequence = "\033[?2004l";
  process_ansi_string(parser, sequence, tb);

  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.bracketed_paste_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_bracketed_paste_toggle) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Initially disabled */
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.bracketed_paste_mode);

  /* Enable */
  process_ansi_string(parser, "\033[?2004h", tb);
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.bracketed_paste_mode);

  /* Disable */
  process_ansi_string(parser, "\033[?2004l", tb);
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.bracketed_paste_mode);

  /* Enable again */
  process_ansi_string(parser, "\033[?2004h", tb);
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.bracketed_paste_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * XTERM: Mouse Reporting Tests
 * ======================================================================== */

TEST(xterm_mouse_normal_tracking_enable) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* XTERM: ESC[?1000h - Enable normal mouse tracking */
  process_ansi_string(parser, "\033[?1000h", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_EQ(modes.mouse_report_mode, 1000);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_mouse_normal_tracking_disable) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable first */
  process_ansi_string(parser, "\033[?1000h", tb);

  /* XTERM: ESC[?1000l - Disable normal mouse tracking */
  process_ansi_string(parser, "\033[?1000l", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_EQ(modes.mouse_report_mode, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_mouse_cell_motion_tracking) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* XTERM: ESC[?1002h - Enable cell motion tracking */
  process_ansi_string(parser, "\033[?1002h", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_EQ(modes.mouse_report_mode, 1002);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_mouse_all_motion_tracking) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[?1003h", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_EQ(modes.mouse_report_mode, 1003);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_mouse_reset_inactive_tracking_mode_keeps_active_mode) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[?1002h\033[?1000l", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_EQ(modes.mouse_report_mode, 1002);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_mouse_sgr_mode_enable) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* XTERM: ESC[?1006h - Enable SGR mouse mode */
  process_ansi_string(parser, "\033[?1006h", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_TRUE(modes.mouse_sgr_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_mouse_sgr_mode_disable) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable first */
  process_ansi_string(parser, "\033[?1006h", tb);

  /* XTERM: ESC[?1006l - Disable SGR mouse mode */
  process_ansi_string(parser, "\033[?1006l", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_FALSE(modes.mouse_sgr_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_mouse_encoding_modes_are_mutually_exclusive) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[?1005h", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_TRUE(modes.mouse_utf8_mode);
  ASSERT_FALSE(modes.mouse_sgr_mode);

  process_ansi_string(parser, "\033[?1006h\033[?1005l", tb);
  ansi_parser_get_modes(parser, &modes);
  ASSERT_FALSE(modes.mouse_utf8_mode);
  ASSERT_TRUE(modes.mouse_sgr_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_alternate_scroll_mode_toggle) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  AnsiParserModes modes;

  ansi_parser_get_modes(parser, &modes);
  ASSERT_TRUE(modes.alternate_scroll_mode);

  process_ansi_string(parser, "\033[?1007l", tb);
  ansi_parser_get_modes(parser, &modes);
  ASSERT_FALSE(modes.alternate_scroll_mode);

  process_ansi_string(parser, "\033[?1007h", tb);
  ansi_parser_get_modes(parser, &modes);
  ASSERT_TRUE(modes.alternate_scroll_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_ris_resets_mouse_modes) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(parser, "\033[?1003h\033[?1006h\033[?1005h\033[?1007l",
                      tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_EQ(modes.mouse_report_mode, 1003);
  ASSERT_TRUE(modes.mouse_utf8_mode);
  ASSERT_FALSE(modes.mouse_sgr_mode);
  ASSERT_FALSE(modes.alternate_scroll_mode);

  process_ansi_string(parser, "\033c", tb);

  ansi_parser_get_modes(parser, &modes);
  ASSERT_EQ(modes.mouse_report_mode, 0);
  ASSERT_FALSE(modes.mouse_utf8_mode);
  ASSERT_FALSE(modes.mouse_sgr_mode);
  ASSERT_TRUE(modes.alternate_scroll_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_ris_exits_alt_screen_and_resets_saved_cursor) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  process_ansi_string(
      parser, "main\033[31m\033[?5h\033[6;11H\0337\033[?1049halt\033c", tb);

  ASSERT_FALSE(terminal_buffer_is_alt_screen(tb));
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)' ');
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->fg_color,
            terminal_buffer_get_config_fg(tb));
  ASSERT_FALSE(terminal_buffer_is_reverse_screen(tb));

  size_t row = 0;
  size_t col = 0;
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  process_ansi_string(parser, "\033[3;4H\0338", tb);
  terminal_buffer_get_cursor(tb, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * XTERM: Application Cursor Keys (alias for DECCKM)
 * ======================================================================== */

TEST(xterm_application_cursor_keys_enable) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* XTERM: ESC[?1h - Enable application cursor keys (same as DECCKM) */
  const char *sequence = "\033[?1h";
  process_ansi_string(parser, sequence, tb);

  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.cursor_keys_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_application_cursor_keys_disable) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable first */
  process_ansi_string(parser, "\033[?1h", tb);
  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.cursor_keys_application_mode);

  /* XTERM: ESC[?1l - Disable application cursor keys */
  const char *sequence = "\033[?1l";
  process_ansi_string(parser, sequence, tb);

  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.cursor_keys_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_focus_event_mode_enable_emits_no_bytes) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  g_captured_reply_len = 0;
  g_captured_reply[0] = '\0';
  ansi_parser_set_reply_callback(parser, capture_reply_bytes, nullptr);

  process_ansi_string(parser, "\033[?1004h", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_TRUE(modes.focus_event_mode);
  ASSERT_EQ(g_captured_reply_len, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_focus_event_mode_disable_emits_no_bytes) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();
  g_captured_reply_len = 0;
  g_captured_reply[0] = '\0';
  ansi_parser_set_reply_callback(parser, capture_reply_bytes, nullptr);

  process_ansi_string(parser, "\033[?1004h", tb);
  process_ansi_string(parser, "\033[?1004l", tb);

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_FALSE(modes.focus_event_mode);
  ASSERT_EQ(g_captured_reply_len, 0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_synchronized_output_toggles_flag) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  ASSERT_FALSE(ansi_parser_is_synchronized(parser));
  process_ansi_string(parser, "\033[?2026h", tb);
  ASSERT_TRUE(ansi_parser_is_synchronized(parser));

  AnsiParserModes modes;
  ansi_parser_get_modes(parser, &modes);
  ASSERT_TRUE(modes.synchronized_output);

  process_ansi_string(parser, "Hello", tb);
  ASSERT_EQ(terminal_buffer_get_cell(tb, 0, 0)->codepoint, (uint32_t)'H');
  ASSERT_TRUE(ansi_parser_is_synchronized(parser));

  process_ansi_string(parser, "\033[?2026l", tb);
  ASSERT_FALSE(ansi_parser_is_synchronized(parser));
  ansi_parser_get_modes(parser, &modes);
  ASSERT_FALSE(modes.synchronized_output);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * XTERM: Integration Tests
 * ======================================================================== */

TEST(xterm_integration_multiple_osc_sequences) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Multiple OSC sequences in sequence */
  process_ansi_string(parser, "\033]0;Window 1\007", tb);
  process_ansi_string(parser, "\033]1;Icon 1\007", tb);
  process_ansi_string(parser, "\033]2;Window 2\007", tb);

  ASSERT_TRUE(true); /* Should handle all without crash */

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_integration_mixed_sequences) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Mix of XTERM and VT sequences */
  process_ansi_string(parser, "\033[2J", tb);              /* Clear screen */
  process_ansi_string(parser, "\033]0;Test\007", tb);      /* Set title */
  process_ansi_string(parser, "\033[H", tb);               /* Home */
  process_ansi_string(parser, "\033[?2004h", tb);          /* Bracketed paste */
  process_ansi_string(parser, "\033[1;31mRed\033[0m", tb); /* Red text */

  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.bracketed_paste_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_integration_mouse_and_paste) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable multiple modes */
  process_ansi_string(parser, "\033[?2004h", tb); /* Bracketed paste */
  process_ansi_string(parser, "\033[?1000h", tb); /* Mouse tracking */
  process_ansi_string(parser, "\033[?1006h", tb); /* SGR mouse */

  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_TRUE(_pm.bracketed_paste_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(xterm_integration_disable_all_modes) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AnsiParser *parser = ansi_parser_init();

  /* Enable everything */
  process_ansi_string(parser, "\033[?2004h", tb);
  process_ansi_string(parser, "\033[?1000h", tb);
  process_ansi_string(parser, "\033[?1006h", tb);
  process_ansi_string(parser, "\033[?1h", tb);

  /* Disable everything */
  process_ansi_string(parser, "\033[?2004l", tb);
  process_ansi_string(parser, "\033[?1000l", tb);
  process_ansi_string(parser, "\033[?1006l", tb);
  process_ansi_string(parser, "\033[?1l", tb);

  AnsiParserModes _pm;
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.bracketed_paste_mode);
  ansi_parser_get_modes(parser, &_pm);
  ASSERT_FALSE(_pm.cursor_keys_application_mode);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

/* ========================================================================
 * Test Registration
 * ======================================================================== */

void run_xterm_tests(void) {
  /* OSC Window Title tests */
  RUN_TEST(xterm_osc_0_set_window_title);
  RUN_TEST(xterm_osc_2_set_window_title);
  RUN_TEST(xterm_osc_1_set_icon_name);
  RUN_TEST(xterm_osc_empty_title);
  RUN_TEST(xterm_osc_long_title);
  RUN_TEST(xterm_osc_title_with_semicolons);
  RUN_TEST(xterm_osc_title_with_special_chars);
  RUN_TEST(xterm_osc_st_terminator);
  RUN_TEST(xterm_osc_rejects_non_numeric_command);
  RUN_TEST(xterm_osc_rejects_overflow_command);

  /* OSC 52 clipboard write */
  RUN_TEST(xterm_osc_52_clipboard_write);
  RUN_TEST(xterm_osc_52_st_terminator);
  RUN_TEST(xterm_osc_52_query_ignored);
  RUN_TEST(xterm_osc_52_invalid_base64);
  RUN_TEST(xterm_osc_52_empty_pd);
  RUN_TEST(xterm_osc_52_overflow_dropped);

  /* OSC 8 hyperlinks */
  RUN_TEST(xterm_osc_8_hyperlink_applies_to_cells);
  RUN_TEST(xterm_osc_8_empty_clears);

  /* Bracketed Paste tests */
  RUN_TEST(xterm_bracketed_paste_mode_enable);
  RUN_TEST(xterm_bracketed_paste_mode_disable);
  RUN_TEST(xterm_bracketed_paste_toggle);

  /* Mouse Reporting tests */
  RUN_TEST(xterm_mouse_normal_tracking_enable);
  RUN_TEST(xterm_mouse_normal_tracking_disable);
  RUN_TEST(xterm_mouse_cell_motion_tracking);
  RUN_TEST(xterm_mouse_all_motion_tracking);
  RUN_TEST(xterm_mouse_reset_inactive_tracking_mode_keeps_active_mode);
  RUN_TEST(xterm_mouse_sgr_mode_enable);
  RUN_TEST(xterm_mouse_sgr_mode_disable);
  RUN_TEST(xterm_mouse_encoding_modes_are_mutually_exclusive);
  RUN_TEST(xterm_alternate_scroll_mode_toggle);
  RUN_TEST(xterm_ris_resets_mouse_modes);
  RUN_TEST(xterm_ris_exits_alt_screen_and_resets_saved_cursor);

  /* Application Cursor Keys tests */
  RUN_TEST(xterm_application_cursor_keys_enable);
  RUN_TEST(xterm_application_cursor_keys_disable);

  /* Focus events / synchronized output */
  RUN_TEST(xterm_focus_event_mode_enable_emits_no_bytes);
  RUN_TEST(xterm_focus_event_mode_disable_emits_no_bytes);
  RUN_TEST(xterm_synchronized_output_toggles_flag);

  /* Integration tests */
  RUN_TEST(xterm_integration_multiple_osc_sequences);
  RUN_TEST(xterm_integration_mixed_sequences);
  RUN_TEST(xterm_integration_mouse_and_paste);
  RUN_TEST(xterm_integration_disable_all_modes);
}
