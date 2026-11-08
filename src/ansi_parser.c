/**
 * ansi_parser.c - ANSI parser implementation
 *
 * Optional: Set MITR_QUANTIZE_TRUE_COLOR=1 to reduce True Color to 5 bits
 * per channel (32^3 = 32768 colors). Improves glyph cache hit rate with
 * syntax-highlighting apps (Helix, etc.) at slight color fidelity cost.
 */
#ifndef MITR_QUANTIZE_TRUE_COLOR
#define MITR_QUANTIZE_TRUE_COLOR 0
#endif

#include "ansi_parser.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#define MAX_PARAMS 16
#define OSC_BUF_CAP 65536
#define OSC_DECODE_CAP ((size_t)48 * 1024)

/* ========================================================================
 * Internal Structure
 * ======================================================================== */

struct AnsiParser {
  ParserState state;
  int params[MAX_PARAMS];
  size_t param_count;
  int current_param;
  char private_prefix; /* 0=none, '?'=DEC, '>'=secondary DA, etc. */
  char csi_intermediate;

  size_t saved_cursor_row;
  size_t saved_cursor_col;

  bool keypad_application_mode;
  bool cursor_keys_application_mode;

  bool bracketed_paste_mode;

  char *osc_buffer;
  size_t osc_capacity;
  size_t osc_length;
  bool osc_overflow;
  bool osc_pending_esc;
  char *osc_decode;

  int mouse_report_mode;
  bool mouse_utf8_mode;
  bool mouse_sgr_mode;
  bool alternate_scroll_mode;

  char charset_g0;
  char charset_g1;
  bool gl_is_g1;
  char charset_designating;

  bool saved_pending_wrap;

  uint32_t utf8_codepoint;
  uint8_t utf8_bytes_remaining;
  uint8_t utf8_bytes_expected;

  uint32_t last_graphic;
  bool last_graphic_valid;

  bool focus_event_mode;
  bool synchronized_output;
  uint32_t sync_generation;

  uint32_t palette[16];
  bool bold_is_bright;

  void (*resize_callback)(void *userdata, size_t rows, size_t cols);
  void *resize_userdata;

  void (*title_callback)(void *userdata, const char *title);
  void *title_userdata;

  void (*reply_callback)(void *userdata, const char *data, size_t len);
  void *reply_userdata;

  void (*clipboard_callback)(void *userdata, const char *text, size_t len);
  void *clipboard_userdata;
};

static void ansi_parser_reset_runtime_modes(AnsiParser *parser) {
  parser->keypad_application_mode = false;
  parser->cursor_keys_application_mode = false;
  parser->bracketed_paste_mode = false;
  parser->mouse_report_mode = 0;
  parser->mouse_utf8_mode = false;
  parser->mouse_sgr_mode = false;
  parser->alternate_scroll_mode = true;
  parser->focus_event_mode = false;
  parser->synchronized_output = false;
}

/* ========================================================================
 * DEC Special Graphics Character Set (line-drawing characters)
 * Maps ASCII 0x60-0x7E to Unicode codepoints when G0/G1 is set to '0'
 * ======================================================================== */

static const uint32_t dec_special_graphics[] = {
    0x25C6, /* 0x60 '`' → ◆ diamond */
    0x2592, /* 0x61 'a' → ▒ checkerboard */
    0x2409, /* 0x62 'b' → ␉ HT symbol */
    0x240C, /* 0x63 'c' → ␌ FF symbol */
    0x240D, /* 0x64 'd' → ␍ CR symbol */
    0x240A, /* 0x65 'e' → ␊ LF symbol */
    0x00B0, /* 0x66 'f' → ° degree */
    0x00B1, /* 0x67 'g' → ± plus-minus */
    0x2424, /* 0x68 'h' → ␤ NL symbol */
    0x240B, /* 0x69 'i' → ␋ VT symbol */
    0x2518, /* 0x6A 'j' → ┘ lower-right corner */
    0x2510, /* 0x6B 'k' → ┐ upper-right corner */
    0x250C, /* 0x6C 'l' → ┌ upper-left corner */
    0x2514, /* 0x6D 'm' → └ lower-left corner */
    0x253C, /* 0x6E 'n' → ┼ crossing lines */
    0x23BA, /* 0x6F 'o' → ⎺ scan line 1 */
    0x23BB, /* 0x70 'p' → ⎻ scan line 3 */
    0x2500, /* 0x71 'q' → ─ horizontal line */
    0x23BC, /* 0x72 'r' → ⎼ scan line 7 */
    0x23BD, /* 0x73 's' → ⎽ scan line 9 */
    0x251C, /* 0x74 't' → ├ left tee */
    0x2524, /* 0x75 'u' → ┤ right tee */
    0x2534, /* 0x76 'v' → ┴ bottom tee */
    0x252C, /* 0x77 'w' → ┬ top tee */
    0x2502, /* 0x78 'x' → │ vertical line */
    0x2264, /* 0x79 'y' → ≤ less-than-or-equal */
    0x2265, /* 0x7A 'z' → ≥ greater-than-or-equal */
    0x03C0, /* 0x7B '{' → π pi */
    0x2260, /* 0x7C '|' → ≠ not-equal */
    0x00A3, /* 0x7D '}' → £ pound */
    0x00B7, /* 0x7E '~' → · bullet */
};

/**
 * Map a character through the active charset.
 * Returns the Unicode codepoint for the given byte.
 */
static uint32_t map_charset(AnsiParser *parser, unsigned char ch) {
  int active = parser->gl_is_g1 ? parser->charset_g1 : parser->charset_g0;
  if (active == '0' && ch >= 0x60 && ch <= 0x7E) {
    return dec_special_graphics[ch - 0x60];
  }
  return (uint32_t)ch;
}

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

#if MITR_QUANTIZE_TRUE_COLOR
/**
 * Quantize 24-bit RGB to 5 bits per channel (32^3 colors).
 * Reduces glyph cache pressure with syntax highlighters.
 */
static inline uint32_t quantize_true_color(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t rq = (uint8_t)(((unsigned)r + 4u) >> 3 << 3);
  uint8_t gq = (uint8_t)(((unsigned)g + 4u) >> 3 << 3);
  uint8_t bq = (uint8_t)(((unsigned)b + 4u) >> 3 << 3);
  return RGB(rq, gq, bq);
}
#endif

/** Saturating CSI digit accumulation — never overflows int. */
static void csi_accumulate_digit(AnsiParser *parser, unsigned char ch) {
  int digit = (int)(ch - '0');
  if (parser->current_param > (INT_MAX - digit) / 10) {
    parser->current_param = INT_MAX;
  } else {
    parser->current_param = parser->current_param * 10 + digit;
  }
}

/** CSI numeric parameter with VT default of 1 when missing or non-positive. */
static int csi_param_default_one(const AnsiParser *parser, size_t index) {
  if (index < parser->param_count && parser->params[index] > 0) {
    return parser->params[index];
  }
  return 1;
}

/**
 * Validate a completed UTF-8 codepoint (overlong, surrogate, range).
 */
static bool utf8_codepoint_ok(uint32_t cp, uint8_t expected_bytes) {
  if (cp > 0x10FFFFU) {
    return false;
  }
  if (cp >= 0xD800U && cp <= 0xDFFFU) {
    return false; /* UTF-16 surrogates */
  }
  if (expected_bytes == 2 && cp < 0x80U) {
    return false; /* overlong */
  }
  if (expected_bytes == 3 && cp < 0x800U) {
    return false;
  }
  if (expected_bytes == 4 && cp < 0x10000U) {
    return false;
  }
  return true;
}

static void emit_graphic(AnsiParser *parser, TerminalBuffer *tb,
                         uint32_t codepoint) {
  parser->last_graphic = codepoint;
  parser->last_graphic_valid = true;
  terminal_buffer_put_codepoint(tb, codepoint);
}

static void utf8_emit_or_replace(AnsiParser *parser, TerminalBuffer *tb) {
  if (utf8_codepoint_ok(parser->utf8_codepoint, parser->utf8_bytes_expected)) {
    emit_graphic(parser, tb, parser->utf8_codepoint);
  } else {
    emit_graphic(parser, tb, 0xFFFD);
  }
  parser->utf8_codepoint = 0;
  parser->utf8_bytes_remaining = 0;
  parser->utf8_bytes_expected = 0;
}

static void utf8_reset(AnsiParser *parser) {
  parser->utf8_codepoint = 0;
  parser->utf8_bytes_remaining = 0;
  parser->utf8_bytes_expected = 0;
}

static void csi_reset_collect(AnsiParser *parser) {
  parser->private_prefix = 0;
  parser->param_count = 0;
  parser->current_param = 0;
  parser->csi_intermediate = 0;
}

/**
 * Convert ANSI color code (0-7) to RGB888 using parser palette
 */
static uint32_t ansi_color_to_rgb(const AnsiParser *parser, int color) {
  if (color >= 0 && color < 8) {
    return parser->palette[color];
  }
  return parser->palette[7];
}

/**
 * Convert ANSI bright color code (0-7) to RGB888 using parser palette
 */
static uint32_t ansi_bright_color_to_rgb(const AnsiParser *parser, int color) {
  if (color >= 0 && color < 8) {
    return parser->palette[8 + color];
  }
  return parser->palette[15];
}

/**
 * VT220: Convert 256-color palette index to RGB
 * 0-15: Standard colors
 * 16-231: 6x6x6 RGB cube
 * 232-255: Grayscale ramp
 */
static uint32_t xterm_256_to_rgb(const AnsiParser *parser, uint8_t color) {
  /* Colors 0-15: Standard 16 colors */
  if (color < 8) {
    return ansi_color_to_rgb(parser, color);
  } else if (color < 16) {
    return ansi_bright_color_to_rgb(parser, color - 8);
  }

  /* Colors 16-231: 6x6x6 RGB cube */
  if (color >= 16 && color <= 231) {
    int index = color - 16;
    int r = (index / 36) % 6;
    int g = (index / 6) % 6;
    int b = index % 6;

    /* Convert 0-5 range to 0-255 */
    uint8_t red = r ? (uint8_t)(r * 40 + 55) : 0;
    uint8_t green = g ? (uint8_t)(g * 40 + 55) : 0;
    uint8_t blue = b ? (uint8_t)(b * 40 + 55) : 0;

    return RGB(red, green, blue);
  }

  /* Colors 232-255: Grayscale ramp */
  if (color >= 232) {
    uint8_t gray = (uint8_t)(8 + (color - 232) * 10);
    return RGB(gray, gray, gray);
  }

  /* Fallback */
  return COLOR_DEFAULT_FG;
}

/**
 * Execute SGR (Select Graphic Rendition) command
 */
static void sgr_reset_defaults(TerminalBuffer *tb) {
  terminal_buffer_set_default_fg(tb, terminal_buffer_get_config_fg(tb));
  terminal_buffer_set_default_bg(tb, terminal_buffer_get_config_bg(tb));
  terminal_buffer_set_current_attr(tb, ATTR_NONE);
}

static void sgr_or_attr(TerminalBuffer *tb, uint8_t bit) {
  terminal_buffer_set_current_attr(tb,
                                   terminal_buffer_get_current_attr(tb) | bit);
}

static void sgr_clear_attr(TerminalBuffer *tb, uint8_t bits) {
  terminal_buffer_set_current_attr(tb, terminal_buffer_get_current_attr(tb) &
                                           (uint8_t)~bits);
}

static size_t sgr_apply_extended(AnsiParser *parser, TerminalBuffer *tb,
                                 size_t i, bool foreground) {
  uint32_t rgb;

  if (i + 1 >= parser->param_count) {
    return i;
  }

  if (parser->params[i + 1] == 5 && i + 2 < parser->param_count) {
    uint8_t color_index = (uint8_t)parser->params[i + 2];
    rgb = xterm_256_to_rgb(parser, color_index);
    if (foreground) {
      terminal_buffer_set_default_fg(tb, rgb);
    } else {
      terminal_buffer_set_default_bg(tb, rgb);
    }
    return i + 2;
  }

  if (parser->params[i + 1] == 2 && i + 4 < parser->param_count) {
    uint8_t r = (uint8_t)parser->params[i + 2];
    uint8_t g = (uint8_t)parser->params[i + 3];
    uint8_t b = (uint8_t)parser->params[i + 4];
#if MITR_QUANTIZE_TRUE_COLOR
    rgb = quantize_true_color(r, g, b);
#else
    rgb = RGB(r, g, b);
#endif
    if (foreground) {
      terminal_buffer_set_default_fg(tb, rgb);
    } else {
      terminal_buffer_set_default_bg(tb, rgb);
    }
    return i + 4;
  }

  return i;
}

static void sgr_apply_one(AnsiParser *parser, TerminalBuffer *tb, int param,
                          size_t *i) {
  if (param == 0) {
    sgr_reset_defaults(tb);
  } else if (param == 1) {
    sgr_or_attr(tb, ATTR_BOLD);
    if (parser->bold_is_bright) {
      for (int c = 0; c < 8; c++) {
        if (terminal_buffer_get_default_fg(tb) == parser->palette[c]) {
          terminal_buffer_set_default_fg(tb, parser->palette[8 + c]);
          break;
        }
      }
    }
  } else if (param == 2) {
    sgr_or_attr(tb, ATTR_DIM);
  } else if (param == 3) {
    sgr_or_attr(tb, ATTR_ITALIC);
  } else if (param == 4) {
    sgr_or_attr(tb, ATTR_UNDERLINE);
  } else if (param == 5) {
    sgr_or_attr(tb, ATTR_BLINK);
  } else if (param == 7) {
    sgr_or_attr(tb, ATTR_REVERSE);
  } else if (param == 8) {
    sgr_or_attr(tb, ATTR_CONCEALED);
  } else if (param == 9) {
    sgr_or_attr(tb, ATTR_STRIKE);
  } else if (param == 22) {
    sgr_clear_attr(tb, ATTR_BOLD | ATTR_DIM);
  } else if (param == 23) {
    sgr_clear_attr(tb, ATTR_ITALIC);
  } else if (param == 24) {
    sgr_clear_attr(tb, ATTR_UNDERLINE);
  } else if (param == 25) {
    sgr_clear_attr(tb, ATTR_BLINK);
  } else if (param == 27) {
    sgr_clear_attr(tb, ATTR_REVERSE);
  } else if (param == 28) {
    sgr_clear_attr(tb, ATTR_CONCEALED);
  } else if (param == 29) {
    sgr_clear_attr(tb, ATTR_STRIKE);
  } else if (param >= 30 && param <= 37) {
    int idx = param - 30;
    if (parser->bold_is_bright &&
        (terminal_buffer_get_current_attr(tb) & ATTR_BOLD)) {
      terminal_buffer_set_default_fg(tb, ansi_bright_color_to_rgb(parser, idx));
    } else {
      terminal_buffer_set_default_fg(tb, ansi_color_to_rgb(parser, idx));
    }
  } else if (param == 38) {
    *i = sgr_apply_extended(parser, tb, *i, true);
  } else if (param == 39) {
    terminal_buffer_set_default_fg(tb, terminal_buffer_get_config_fg(tb));
  } else if (param >= 40 && param <= 47) {
    terminal_buffer_set_default_bg(tb, ansi_color_to_rgb(parser, param - 40));
  } else if (param == 48) {
    *i = sgr_apply_extended(parser, tb, *i, false);
  } else if (param == 49) {
    terminal_buffer_set_default_bg(tb, terminal_buffer_get_config_bg(tb));
  } else if (param >= 90 && param <= 97) {
    terminal_buffer_set_default_fg(
        tb, ansi_bright_color_to_rgb(parser, param - 90));
  } else if (param >= 100 && param <= 107) {
    terminal_buffer_set_default_bg(
        tb, ansi_bright_color_to_rgb(parser, param - 100));
  }
}

static void execute_sgr(AnsiParser *parser, TerminalBuffer *tb) {
  if (parser->param_count == 0) {
    sgr_reset_defaults(tb);
    return;
  }

  for (size_t i = 0; i < parser->param_count; i++) {
    sgr_apply_one(parser, tb, parser->params[i], &i);
  }
}

/**
 * Execute CSI sequence
 */
static void apply_dec_mode(AnsiParser *parser, TerminalBuffer *tb, int mode,
                           bool set) {
  switch (mode) {
  case 1049:
  case 47:
    if (set) {
      terminal_buffer_enter_alt_screen(tb);
    } else {
      terminal_buffer_exit_alt_screen(tb);
    }
    break;
  case 6:
    terminal_buffer_set_origin_mode(tb, set);
    terminal_buffer_set_cursor(tb, 0, 0);
    break;
  case 7:
    terminal_buffer_set_auto_wrap(tb, set);
    break;
  case 25:
    terminal_buffer_set_cursor_visible(tb, set);
    break;
  case 1:
    parser->cursor_keys_application_mode = set;
    break;
  case 1000:
  case 1002:
  case 1003:
    if (set) {
      parser->mouse_report_mode = mode;
    } else if (parser->mouse_report_mode == mode) {
      parser->mouse_report_mode = 0;
    }
    break;
  case 1005:
    parser->mouse_utf8_mode = set;
    if (set) {
      parser->mouse_sgr_mode = false;
    }
    break;
  case 1006:
    parser->mouse_sgr_mode = set;
    if (set) {
      parser->mouse_utf8_mode = false;
    }
    break;
  case 1007:
    parser->alternate_scroll_mode = set;
    break;
  case 2004:
    parser->bracketed_paste_mode = set;
    break;
  case 1004:
    parser->focus_event_mode = set;
    break;
  case 2026:
    parser->synchronized_output = set;
    if (set) {
      parser->sync_generation++;
    }
    break;
  case 3: {
    size_t new_cols = set ? 132 : 80;
    if (terminal_buffer_resize_columns(tb, new_cols) == MITR_OK &&
        parser->resize_callback) {
      parser->resize_callback(parser->resize_userdata,
                              terminal_buffer_get_rows(tb),
                              terminal_buffer_get_cols(tb));
    }
    break;
  }
  case 5:
    terminal_buffer_set_reverse_screen(tb, set);
    break;
  default:
    break;
  }
}

static void execute_dec_private(AnsiParser *parser, char final,
                                TerminalBuffer *tb) {
  if (final != 'h' && final != 'l') {
    return;
  }

  bool set = (final == 'h');
  for (size_t i = 0; i < parser->param_count; i++) {
    apply_dec_mode(parser, tb, parser->params[i], set);
  }
}

static void clear_cell_at(TerminalBuffer *tb, size_t row, size_t col) {
  TermCell *cell = terminal_buffer_get_cell(tb, row, col);
  if (!cell) {
    return;
  }
  cell->codepoint = ' ';
  cell->fg_color = terminal_buffer_get_default_fg(tb);
  cell->bg_color = terminal_buffer_get_default_bg(tb);
  cell->attributes = ATTR_NONE;
  terminal_buffer_mark_row_dirty(tb, row);
}

static void csi_erase_in_display(AnsiParser *parser, TerminalBuffer *tb) {
  int mode = (parser->param_count > 0) ? parser->params[0] : 0;
  size_t cursor_row;
  size_t cursor_col;
  terminal_buffer_get_cursor(tb, &cursor_row, &cursor_col);

  if (mode == 3) {
    return;
  }
  if (mode == 2) {
    terminal_buffer_clear(tb);
    return;
  }

  if (mode == 0) {
    for (size_t col = cursor_col; col < terminal_buffer_get_cols(tb); col++) {
      clear_cell_at(tb, cursor_row, col);
    }
    for (size_t row = cursor_row + 1; row < terminal_buffer_get_rows(tb);
         row++) {
      for (size_t col = 0; col < terminal_buffer_get_cols(tb); col++) {
        clear_cell_at(tb, row, col);
      }
    }
    return;
  }

  if (mode == 1) {
    for (size_t row = 0; row < cursor_row; row++) {
      for (size_t col = 0; col < terminal_buffer_get_cols(tb); col++) {
        clear_cell_at(tb, row, col);
      }
    }
    for (size_t col = 0; col <= cursor_col; col++) {
      clear_cell_at(tb, cursor_row, col);
    }
  }
}

static void csi_erase_in_line(AnsiParser *parser, TerminalBuffer *tb) {
  int mode = (parser->param_count > 0) ? parser->params[0] : 0;
  size_t row;
  size_t col;
  terminal_buffer_get_cursor(tb, &row, &col);

  size_t start = 0;
  size_t end = terminal_buffer_get_cols(tb);
  if (mode == 0) {
    start = col;
  } else if (mode == 1) {
    end = col + 1;
  } else if (mode != 2) {
    return;
  }

  for (size_t i = start; i < end; i++) {
    clear_cell_at(tb, row, i);
  }
}

static void csi_set_scroll_region(AnsiParser *parser, TerminalBuffer *tb) {
  if (parser->param_count == 0) {
    terminal_buffer_reset_scroll_region(tb);
    terminal_buffer_set_cursor(tb, 0, 0);
    return;
  }

  size_t top = 0;
  size_t bottom = terminal_buffer_get_rows(tb) - 1;
  if (parser->param_count >= 2) {
    if (parser->params[0] > 0) {
      top = (size_t)(parser->params[0] - 1);
    }
    if (parser->params[1] > 0) {
      bottom = (size_t)(parser->params[1] - 1);
    }
  }
  terminal_buffer_set_scroll_region(tb, top, bottom);
  terminal_buffer_set_cursor(tb, 0, 0);
}

static void parser_emit_reply(AnsiParser *parser, const char *data,
                              size_t len) {
  if (!parser->reply_callback || !data || len == 0) {
    return;
  }
  parser->reply_callback(parser->reply_userdata, data, len);
}

static void csi_device_attributes(AnsiParser *parser) {
  char buf[32];
  int n;

  if (parser->private_prefix == '>') {
    n = snprintf(buf, sizeof(buf), "\033[>0;0;0c");
    if (n > 0 && (size_t)n < sizeof(buf)) {
      log_debug("DA secondary reply");
      parser_emit_reply(parser, buf, (size_t)n);
    }
    return;
  }

  if (parser->private_prefix != 0) {
    return;
  }

  /* Primary DA: parameter 0 or absent */
  if (parser->param_count > 0 && parser->params[0] != 0) {
    return;
  }

  n = snprintf(buf, sizeof(buf), "\033[?1;2c");
  if (n > 0 && (size_t)n < sizeof(buf)) {
    log_debug("DA primary reply");
    parser_emit_reply(parser, buf, (size_t)n);
  }
}

static void csi_device_status(AnsiParser *parser, TerminalBuffer *tb) {
  char buf[32];
  int n;
  int param = (parser->param_count > 0) ? parser->params[0] : 0;

  if (parser->private_prefix != 0) {
    return;
  }

  if (param == 5) {
    n = snprintf(buf, sizeof(buf), "\033[0n");
    if (n > 0 && (size_t)n < sizeof(buf)) {
      log_debug("DSR status reply");
      parser_emit_reply(parser, buf, (size_t)n);
    }
    return;
  }

  if (param != 6) {
    return;
  }

  size_t row = 0;
  size_t col = 0;
  terminal_buffer_get_cursor(tb, &row, &col);
  if (terminal_buffer_is_origin_mode(tb)) {
    size_t top = 0;
    terminal_buffer_get_scroll_region(tb, &top, nullptr);
    row = (row >= top) ? (row - top) : 0;
  }

  n = snprintf(buf, sizeof(buf), "\033[%d;%dR", (int)row + 1, (int)col + 1);
  if (n > 0 && (size_t)n < sizeof(buf)) {
    log_debug("DSR cursor position reply");
    parser_emit_reply(parser, buf, (size_t)n);
  }
}

static int csi_param_bounded(const AnsiParser *parser, int cap) {
  int n = csi_param_default_one(parser, 0);
  return n > cap ? cap : n;
}

static void apply_ansi_mode(TerminalBuffer *tb, int mode, bool set) {
  if (mode == 4) {
    terminal_buffer_set_insert_mode(tb, set);
  }
}

static void execute_sm_rm(AnsiParser *parser, TerminalBuffer *tb, bool set) {
  for (size_t i = 0; i < parser->param_count; i++) {
    apply_ansi_mode(tb, parser->params[i], set);
  }
}

static void csi_repeat_last_graphic(AnsiParser *parser, TerminalBuffer *tb) {
  int count;
  if (!parser->last_graphic_valid) {
    return;
  }
  count = csi_param_bounded(parser, 65535);
  for (int i = 0; i < count; i++) {
    terminal_buffer_put_codepoint(tb, parser->last_graphic);
  }
}

static void csi_cursor_backward_tab(AnsiParser *parser, TerminalBuffer *tb) {
  int count = csi_param_bounded(parser, 1024);
  size_t row;
  terminal_buffer_get_cursor(tb, &row, nullptr);
  /* Column is absolute; convert row so DECOM does not shift it again. */
  if (terminal_buffer_is_origin_mode(tb)) {
    size_t top = 0;
    terminal_buffer_get_scroll_region(tb, &top, nullptr);
    row = (row >= top) ? (row - top) : 0;
  }
  for (int i = 0; i < count; i++) {
    terminal_buffer_set_cursor(tb, row, terminal_buffer_get_prev_tab_stop(tb));
  }
}

static void execute_csi(AnsiParser *parser, char final, TerminalBuffer *tb) {
  int count;
  size_t row;
  size_t col;

  terminal_buffer_set_pending_wrap(tb, false);

  if (parser->private_prefix == '?') {
    execute_dec_private(parser, final, tb);
    return;
  }

  switch (final) {
  case 'm':
    execute_sgr(parser, tb);
    break;
  case 'A':
    count = csi_param_default_one(parser, 0);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(
        tb, (row >= (size_t)count) ? row - (size_t)count : 0, col);
    break;
  case 'B':
    count = csi_param_default_one(parser, 0);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(tb, row + (size_t)count, col);
    break;
  case 'C':
    count = csi_param_default_one(parser, 0);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(tb, row, col + (size_t)count);
    break;
  case 'D':
    count = csi_param_default_one(parser, 0);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(
        tb, row, (col >= (size_t)count) ? col - (size_t)count : 0);
    break;
  case 'H':
  case 'f':
    row = 0;
    col = 0;
    if (parser->param_count >= 1 && parser->params[0] > 0) {
      row = (size_t)(parser->params[0] - 1);
    }
    if (parser->param_count >= 2 && parser->params[1] > 0) {
      col = (size_t)(parser->params[1] - 1);
    }
    terminal_buffer_set_cursor(tb, row, col);
    break;
  case 'J':
    csi_erase_in_display(parser, tb);
    break;
  case 'K':
    csi_erase_in_line(parser, tb);
    break;
  case 's':
    terminal_buffer_get_cursor(tb, &parser->saved_cursor_row,
                               &parser->saved_cursor_col);
    break;
  case 'u':
    terminal_buffer_set_cursor(tb, parser->saved_cursor_row,
                               parser->saved_cursor_col);
    break;
  case 'r':
    csi_set_scroll_region(parser, tb);
    break;
  case 'L':
    terminal_buffer_insert_lines(tb, (size_t)csi_param_default_one(parser, 0));
    break;
  case 'M':
    terminal_buffer_delete_lines(tb, (size_t)csi_param_default_one(parser, 0));
    break;
  case '@':
    terminal_buffer_insert_chars(tb, (size_t)csi_param_default_one(parser, 0));
    break;
  case 'P':
    terminal_buffer_delete_chars(tb, (size_t)csi_param_default_one(parser, 0));
    break;
  case 'X':
    terminal_buffer_erase_chars(tb, (size_t)csi_param_default_one(parser, 0));
    break;
  case 'G':
  case '`':
    count = csi_param_default_one(parser, 0);
    terminal_buffer_get_cursor(tb, &row, &col);
    /* Column is absolute; convert row so DECOM does not shift it again. */
    if (terminal_buffer_is_origin_mode(tb)) {
      size_t top = 0;
      terminal_buffer_get_scroll_region(tb, &top, nullptr);
      row = (row >= top) ? (row - top) : 0;
    }
    terminal_buffer_set_cursor(tb, row, (size_t)(count - 1));
    break;
  case 'b':
    csi_repeat_last_graphic(parser, tb);
    break;
  case 'Z':
    csi_cursor_backward_tab(parser, tb);
    break;
  case 'h':
    if (parser->private_prefix == 0) {
      execute_sm_rm(parser, tb, true);
    }
    break;
  case 'l':
    if (parser->private_prefix == 0) {
      execute_sm_rm(parser, tb, false);
    }
    break;
  case 'd':
    count = csi_param_default_one(parser, 0);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(tb, (size_t)(count - 1), col);
    break;
  case 'E':
    count = csi_param_default_one(parser, 0);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(tb, row + (size_t)count, 0);
    break;
  case 'F':
    count = csi_param_default_one(parser, 0);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(
        tb, (row >= (size_t)count) ? row - (size_t)count : 0, 0);
    break;
  case 'S':
    terminal_buffer_scroll_up_n(tb, (size_t)csi_param_default_one(parser, 0));
    break;
  case 'T':
    terminal_buffer_scroll_down_n(tb, (size_t)csi_param_default_one(parser, 0));
    break;
  case 'g':
    count = (parser->param_count > 0) ? parser->params[0] : 0;
    if (count == 0) {
      terminal_buffer_clear_tab_stop(tb);
    } else if (count == 3) {
      terminal_buffer_clear_all_tab_stops(tb);
    }
    break;
  case 'p':
    if (parser->csi_intermediate == '!') {
      terminal_buffer_soft_reset(tb);
    }
    break;
  case 'c':
    csi_device_attributes(parser);
    break;
  case 'n':
    csi_device_status(parser, tb);
    break;
  default:
    log_debug("Unhandled CSI sequence: %c", final);
    break;
  }
}

static bool osc_is_ws(unsigned char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static int base64_digit(unsigned char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return (int)(ch - 'A');
  }
  if (ch >= 'a' && ch <= 'z') {
    return (int)(ch - 'a') + 26;
  }
  if (ch >= '0' && ch <= '9') {
    return (int)(ch - '0') + 52;
  }
  if (ch == '+') {
    return 62;
  }
  if (ch == '/') {
    return 63;
  }
  return -1;
}

static bool base64_emit_quad(const int digits[4], int pads, char *out,
                             size_t out_cap, size_t *out_len) {
  uint32_t v;
  size_t emit;
  size_t n;

  if (pads < 0 || pads > 2 || digits[0] < 0 || digits[1] < 0) {
    return false;
  }
  if (pads < 2 && digits[2] < 0) {
    return false;
  }
  if (pads < 1 && digits[3] < 0) {
    return false;
  }
  if (pads == 2 && (digits[1] & 0x0F) != 0) {
    return false;
  }
  if (pads == 1 && (digits[2] & 0x03) != 0) {
    return false;
  }

  v = ((uint32_t)digits[0] << 18) | ((uint32_t)digits[1] << 12);
  if (pads < 2) {
    v |= (uint32_t)digits[2] << 6;
  }
  if (pads < 1) {
    v |= (uint32_t)digits[3];
  }

  emit = (size_t)(3 - pads);
  n = *out_len;
  if (n + emit > out_cap) {
    return false;
  }
  out[n] = (char)((v >> 16) & 0xFFu);
  if (emit > 1) {
    out[n + 1] = (char)((v >> 8) & 0xFFu);
  }
  if (emit > 2) {
    out[n + 2] = (char)(v & 0xFFu);
  }
  *out_len = n + emit;
  return true;
}

/* Standard base64; optional whitespace. No allocation. */
static bool base64_decode(const char *in, char *out, size_t out_cap,
                          size_t *out_len) {
  int digits[4];
  int nfill = 0;
  int pads = 0;

  if (!in || !out || !out_len) {
    return false;
  }

  *out_len = 0;

  for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; p++) {
    int d;

    if (osc_is_ws(*p)) {
      continue;
    }
    if (*p == '=') {
      if (nfill < 2 || pads >= 2) {
        return false;
      }
      digits[nfill++] = 0;
      pads++;
      if (nfill == 4) {
        if (!base64_emit_quad(digits, pads, out, out_cap, out_len)) {
          return false;
        }
        nfill = 0;
        pads = 0;
      }
      continue;
    }
    if (pads > 0) {
      return false;
    }
    d = base64_digit(*p);
    if (d < 0) {
      return false;
    }
    digits[nfill++] = d;
    if (nfill == 4) {
      if (!base64_emit_quad(digits, 0, out, out_cap, out_len)) {
        return false;
      }
      nfill = 0;
    }
  }

  return nfill == 0;
}

static bool osc52_pc_is_clipboard(const char *pc) {
  if (!pc || pc[0] == '\0') {
    return true;
  }
  if (pc[0] == 's' && pc[1] == '0' && pc[2] == '\0') {
    return true;
  }
  return strchr(pc, 'c') != nullptr;
}

static bool osc52_pd_is_query(const char *pd) {
  const char *p = pd;

  while (*p != '\0' && osc_is_ws((unsigned char)*p)) {
    p++;
  }
  if (*p != '?') {
    return false;
  }
  p++;
  while (*p != '\0' && osc_is_ws((unsigned char)*p)) {
    p++;
  }
  return *p == '\0';
}

static void execute_osc_52(AnsiParser *parser, char *body) {
  char *pc = body;
  char *pd;
  char *semi;
  size_t decoded_len = 0;

  if (!parser || !body) {
    return;
  }

  semi = strchr(body, ';');
  if (semi) {
    *semi = '\0';
    pd = semi + 1;
  } else {
    pd = "";
  }

  if (!osc52_pc_is_clipboard(pc)) {
    log_debug("XTERM: OSC 52 - ignoring non-clipboard selection");
    return;
  }
  if (pd[0] == '\0') {
    return;
  }
  if (osc52_pd_is_query(pd)) {
    log_debug("XTERM: OSC 52 - clipboard query ignored");
    return;
  }
  if (!base64_decode(pd, parser->osc_decode, OSC_DECODE_CAP, &decoded_len)) {
    return;
  }
  if (decoded_len == 0) {
    return;
  }

  parser->osc_decode[decoded_len] = '\0';
  if (parser->clipboard_callback) {
    parser->clipboard_callback(parser->clipboard_userdata, parser->osc_decode,
                               decoded_len);
  } else {
    log_debug("XTERM: OSC 52 - clipboard write dropped (no callback)");
  }
}

static void osc_reset_collect(AnsiParser *parser) {
  parser->osc_length = 0;
  parser->osc_overflow = false;
  parser->osc_pending_esc = false;
}

/**
 * Parse OSC Ps as a decimal integer. Rejects overflow, trailing junk, and
 * empty command numbers. Valid range is 0..65535 (XTerm uses small Ps).
 */
static bool parse_osc_command_number(const char *s, unsigned long *out) {
  char *end = nullptr;
  unsigned long value;

  if (!s || s[0] == '\0' || !out) {
    return false;
  }

  errno = 0;
  value = strtoul(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0' || value > 65535UL) {
    return false;
  }

  *out = value;
  return true;
}

/**
 * XTERM: Execute OSC (Operating System Command) sequence
 */
static void execute_osc(AnsiParser *parser, TerminalBuffer *tb) {
  if (!parser || !tb || parser->osc_length == 0) {
    return;
  }

  /* Parse OSC command: OSC {Ps};{Pt} BEL/ST
   * Ps = command number
   * Pt = text parameter
   */
  char *buffer = parser->osc_buffer;
  char *semicolon = strchr(buffer, ';');

  unsigned long command = 0;
  char *text = parser->osc_buffer + parser->osc_length;
  const char *ps = buffer;

  if (semicolon) {
    *semicolon = '\0';
    text = semicolon + 1;
  }

  if (!parse_osc_command_number(ps, &command)) {
    log_debug("XTERM: Ignoring OSC with invalid command number");
    return;
  }

  /* XTERM: OSC commands */
  switch (command) {
  case 0:
    /* Set window and icon title */
    log_debug("XTERM: OSC 0 - Set window/icon title: '%s'", text);
    if (parser->title_callback) {
      parser->title_callback(parser->title_userdata, text);
    }
    break;

  case 1:
    /* Set icon name */
    log_debug("XTERM: OSC 1 - Set icon name: '%s'", text);
    /* Icon name (no-op for SDL2) */
    break;

  case 2:
    /* Set window title */
    log_debug("XTERM: OSC 2 - Set window title: '%s'", text);
    if (parser->title_callback) {
      parser->title_callback(parser->title_userdata, text);
    }
    break;

  case 52:
    execute_osc_52(parser, text);
    break;

  case 8: {
    /* OSC 8 ; params ; URI — params (id=...) ignored for v1 */
    char *uri = nullptr;
    char *semi = strchr(text, ';');
    if (semi) {
      uri = semi + 1;
    }
    if (!uri || uri[0] == '\0') {
      terminal_buffer_set_hyperlink(tb, nullptr);
    } else {
      terminal_buffer_set_hyperlink(tb, uri);
    }
    break;
  }

  default:
    log_debug("XTERM: Unknown OSC command: %d", command);
    break;
  }
}

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

AnsiParser *ansi_parser_init(void) {
  log_trace("Initializing ANSI parser");

  AnsiParser *parser = checked_calloc(1, sizeof(AnsiParser));
  if (!parser) {
    return nullptr;
  }

  parser->state = PARSER_STATE_GROUND;
  parser->param_count = 0;
  parser->current_param = 0;
  parser->private_prefix = 0;
  parser->csi_intermediate = 0;
  parser->saved_cursor_row = 0;
  parser->saved_cursor_col = 0;

  /* VT320/XTERM: Initialize input modes. */
  ansi_parser_reset_runtime_modes(parser);

  /* VT100: Initialize character set state */
  parser->charset_g0 = 'B'; /* US ASCII */
  parser->charset_g1 = 'B'; /* US ASCII */
  parser->gl_is_g1 = false; /* GL maps to G0 by default */
  parser->charset_designating = '(';
  parser->saved_pending_wrap = false;

  parser->osc_capacity = OSC_BUF_CAP;
  parser->osc_buffer = checked_malloc(OSC_BUF_CAP);
  if (!parser->osc_buffer) {
    free(parser);
    return nullptr;
  }
  parser->osc_buffer[0] = '\0';
  parser->osc_decode = checked_malloc(OSC_DECODE_CAP + 1);
  if (!parser->osc_decode) {
    SAFE_FREE(parser->osc_buffer);
    free(parser);
    return nullptr;
  }
  osc_reset_collect(parser);
  /* UTF-8: Initialize decoder state */
  parser->utf8_codepoint = 0;
  parser->utf8_bytes_remaining = 0;
  parser->utf8_bytes_expected = 0;
  parser->last_graphic = 0;
  parser->last_graphic_valid = false;
  parser->sync_generation = 0;

  /* Default ANSI palette (overridable via ansi_parser_set_palette) */
  parser->palette[0] = COLOR_BLACK;
  parser->palette[1] = COLOR_RED;
  parser->palette[2] = COLOR_GREEN;
  parser->palette[3] = COLOR_YELLOW;
  parser->palette[4] = COLOR_BLUE;
  parser->palette[5] = COLOR_MAGENTA;
  parser->palette[6] = COLOR_CYAN;
  parser->palette[7] = COLOR_WHITE;
  parser->palette[8] = COLOR_BRIGHT_BLACK;
  parser->palette[9] = COLOR_BRIGHT_RED;
  parser->palette[10] = COLOR_BRIGHT_GREEN;
  parser->palette[11] = COLOR_BRIGHT_YELLOW;
  parser->palette[12] = COLOR_BRIGHT_BLUE;
  parser->palette[13] = COLOR_BRIGHT_MAGENTA;
  parser->palette[14] = COLOR_BRIGHT_CYAN;
  parser->palette[15] = COLOR_BRIGHT_WHITE;
  parser->bold_is_bright = true;

  log_debug("ANSI parser initialized");

  return parser;
}

void ansi_parser_destroy(AnsiParser *parser) {
  if (!parser) {
    return;
  }

  log_trace("Destroying ANSI parser");
  SAFE_FREE(parser->osc_buffer);
  SAFE_FREE(parser->osc_decode);
  free(parser);
}

void ansi_parser_reset(AnsiParser *parser) {
  if (!parser) {
    return;
  }

  parser->state = PARSER_STATE_GROUND;
  csi_reset_collect(parser);
  utf8_reset(parser);
  osc_reset_collect(parser);
}

void ansi_parser_set_palette(AnsiParser *parser, const uint32_t colors[16],
                             bool bold_is_bright) {
  if (!parser)
    return;

  if (colors) {
    for (int i = 0; i < 16; i++)
      parser->palette[i] = colors[i];
  }
  parser->bold_is_bright = bold_is_bright;
}

void ansi_parser_set_resize_callback(AnsiParser *parser,
                                     void (*callback)(void *userdata,
                                                      size_t rows, size_t cols),
                                     void *userdata) {
  if (!parser)
    return;
  parser->resize_callback = callback;
  parser->resize_userdata = userdata;
}

void ansi_parser_set_title_callback(AnsiParser *parser,
                                    void (*callback)(void *userdata,
                                                     const char *title),
                                    void *userdata) {
  if (!parser)
    return;
  parser->title_callback = callback;
  parser->title_userdata = userdata;
}

void ansi_parser_set_reply_callback(AnsiParser *parser,
                                    void (*callback)(void *userdata,
                                                     const char *data,
                                                     size_t len),
                                    void *userdata) {
  if (!parser)
    return;
  parser->reply_callback = callback;
  parser->reply_userdata = userdata;
}

void ansi_parser_set_clipboard_callback(AnsiParser *parser,
                                        void (*callback)(void *userdata,
                                                         const char *text,
                                                         size_t len),
                                        void *userdata) {
  if (!parser)
    return;
  parser->clipboard_callback = callback;
  parser->clipboard_userdata = userdata;
}

static bool feed_utf8_continue(AnsiParser *parser, TerminalBuffer *tb,
                               unsigned char ch) {
  if (parser->utf8_bytes_remaining == 0) {
    return false;
  }

  if (parser->state != PARSER_STATE_GROUND) {
    emit_graphic(parser, tb, 0xFFFD);
    utf8_reset(parser);
    return false;
  }

  if ((ch & 0xC0) == 0x80) {
    parser->utf8_codepoint = (parser->utf8_codepoint << 6) | (ch & 0x3F);
    parser->utf8_bytes_remaining--;
    if (parser->utf8_bytes_remaining == 0) {
      utf8_emit_or_replace(parser, tb);
    }
    return true;
  }

  emit_graphic(parser, tb, 0xFFFD);
  utf8_reset(parser);
  return false;
}

static void feed_ground(AnsiParser *parser, TerminalBuffer *tb,
                        unsigned char ch) {
  size_t row;
  size_t col;

  if (ch == '\x1b') {
    parser->state = PARSER_STATE_ESCAPE;
    csi_reset_collect(parser);
    for (int j = 0; j < MAX_PARAMS; j++) {
      parser->params[j] = 0;
    }
    return;
  }
  if (ch == '\r') {
    terminal_buffer_set_pending_wrap(tb, false);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(tb, row, 0);
    return;
  }
  if (ch == '\n') {
    terminal_buffer_set_pending_wrap(tb, false);
    terminal_buffer_index(tb);
    return;
  }
  if (ch == '\b' || ch == 0x08) {
    terminal_buffer_set_pending_wrap(tb, false);
    terminal_buffer_get_cursor(tb, &row, &col);
    if (col > 0) {
      terminal_buffer_set_cursor(tb, row, col - 1);
    }
    return;
  }
  if (ch == '\t') {
    terminal_buffer_set_pending_wrap(tb, false);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(tb, row, terminal_buffer_get_next_tab_stop(tb));
    return;
  }
  if (ch == 0x07) {
    return;
  }
  if (ch == 0x0E) {
    parser->gl_is_g1 = true;
    return;
  }
  if (ch == 0x0F) {
    parser->gl_is_g1 = false;
    return;
  }
  if (ch >= 0x80) {
    if (ch >= 0xC2 && ch <= 0xDF) {
      parser->utf8_codepoint = ch & 0x1F;
      parser->utf8_bytes_remaining = 1;
      parser->utf8_bytes_expected = 2;
    } else if ((ch & 0xF0) == 0xE0) {
      parser->utf8_codepoint = ch & 0x0F;
      parser->utf8_bytes_remaining = 2;
      parser->utf8_bytes_expected = 3;
    } else if (ch >= 0xF0 && ch <= 0xF4) {
      parser->utf8_codepoint = ch & 0x07;
      parser->utf8_bytes_remaining = 3;
      parser->utf8_bytes_expected = 4;
    } else {
      emit_graphic(parser, tb, 0xFFFD);
      utf8_reset(parser);
    }
    return;
  }
  if (ch >= 32 && ch < 127) {
    emit_graphic(parser, tb, map_charset(parser, ch));
  }
}

static void feed_escape(AnsiParser *parser, TerminalBuffer *tb,
                        unsigned char ch) {
  size_t row;
  size_t col;

  if (ch == '[') {
    parser->state = PARSER_STATE_CSI;
    return;
  }
  if (ch == ']') {
    osc_reset_collect(parser);
    parser->state = PARSER_STATE_OSC;
    return;
  }
  if (ch == 'P' || ch == '_' || ch == '^' || ch == 'X') {
    /* DCS / APC / PM / SOS — discard until ST */
    parser->state = PARSER_STATE_IGNORE_ST;
    parser->osc_length = 0;
    return;
  }
  if (ch == 'D') {
    terminal_buffer_set_pending_wrap(tb, false);
    terminal_buffer_index(tb);
    parser->state = PARSER_STATE_GROUND;
    return;
  }
  if (ch == 'M') {
    terminal_buffer_set_pending_wrap(tb, false);
    terminal_buffer_reverse_index(tb);
    parser->state = PARSER_STATE_GROUND;
    return;
  }
  if (ch == 'E') {
    terminal_buffer_set_pending_wrap(tb, false);
    terminal_buffer_get_cursor(tb, &row, &col);
    terminal_buffer_set_cursor(tb, row, 0);
    terminal_buffer_index(tb);
    parser->state = PARSER_STATE_GROUND;
    return;
  }
  if (ch == '7') {
    terminal_buffer_get_cursor(tb, &parser->saved_cursor_row,
                               &parser->saved_cursor_col);
    parser->saved_pending_wrap = terminal_buffer_is_pending_wrap(tb);
    parser->state = PARSER_STATE_GROUND;
    return;
  }
  if (ch == '8') {
    terminal_buffer_set_cursor(tb, parser->saved_cursor_row,
                               parser->saved_cursor_col);
    terminal_buffer_set_pending_wrap(tb, parser->saved_pending_wrap);
    parser->state = PARSER_STATE_GROUND;
    return;
  }
  if (ch == 'H') {
    terminal_buffer_set_tab_stop(tb);
    parser->state = PARSER_STATE_GROUND;
    return;
  }
  if (ch == '=') {
    parser->keypad_application_mode = true;
    parser->state = PARSER_STATE_GROUND;
    return;
  }
  if (ch == '>') {
    parser->keypad_application_mode = false;
    parser->state = PARSER_STATE_GROUND;
    return;
  }
  if (ch == 'c') {
    terminal_buffer_exit_alt_screen(tb);
    terminal_buffer_soft_reset(tb);
    terminal_buffer_set_reverse_screen(tb, false);
    terminal_buffer_set_hyperlink(tb, nullptr);
    terminal_buffer_clear(tb);
    ansi_parser_reset_runtime_modes(parser);
    parser->charset_g0 = 'B';
    parser->charset_g1 = 'B';
    parser->gl_is_g1 = false;
    parser->saved_cursor_row = 0;
    parser->saved_cursor_col = 0;
    parser->saved_pending_wrap = false;
    parser->state = PARSER_STATE_GROUND;
    return;
  }
  if (ch == '(' || ch == ')') {
    parser->charset_designating = (char)ch;
    parser->state = PARSER_STATE_CHARSET;
    return;
  }
  parser->state = PARSER_STATE_GROUND;
}

static void feed_csi_byte(AnsiParser *parser, TerminalBuffer *tb,
                          unsigned char ch, bool in_param) {
  if (!in_param && (ch == '?' || ch == '>' || ch == '<' || ch == '=')) {
    parser->private_prefix = (char)ch;
    return;
  }
  if (ch >= '0' && ch <= '9') {
    csi_accumulate_digit(parser, ch);
    parser->state = PARSER_STATE_CSI_PARAM;
    return;
  }
  if (ch == ';' || (in_param && ch == ':')) {
    if (parser->param_count < MAX_PARAMS) {
      parser->params[parser->param_count++] =
          in_param ? parser->current_param : 0;
      parser->current_param = 0;
    }
    return;
  }
  if (ch >= 0x20 && ch <= 0x2F) {
    parser->csi_intermediate = (char)ch;
    return;
  }
  if (ch >= 0x40 && ch <= 0x7E) {
    if (in_param && parser->param_count < MAX_PARAMS) {
      parser->params[parser->param_count++] = parser->current_param;
    }
    execute_csi(parser, (char)ch, tb);
    parser->state = PARSER_STATE_GROUND;
    csi_reset_collect(parser);
    return;
  }
  parser->state = PARSER_STATE_GROUND;
  csi_reset_collect(parser);
}

static void osc_finish(AnsiParser *parser, TerminalBuffer *tb) {
  if (!parser->osc_overflow) {
    parser->osc_buffer[parser->osc_length] = '\0';
    execute_osc(parser, tb);
  } else {
    log_debug("XTERM: Dropping OSC that exceeded %zu-byte buffer",
              parser->osc_capacity);
  }
  osc_reset_collect(parser);
  parser->state = PARSER_STATE_GROUND;
}

static void feed_osc(AnsiParser *parser, TerminalBuffer *tb, unsigned char ch) {
  if (ch == 0x07) {
    osc_finish(parser, tb);
    return;
  }
  if (ch == '\\' && parser->osc_pending_esc) {
    if (parser->osc_length > 0 &&
        parser->osc_buffer[parser->osc_length - 1] == 0x1B) {
      parser->osc_length--;
    }
    osc_finish(parser, tb);
    return;
  }
  if (parser->osc_length < parser->osc_capacity - 1) {
    parser->osc_buffer[parser->osc_length++] = (char)ch;
  } else {
    parser->osc_overflow = true;
  }
  parser->osc_pending_esc = (ch == 0x1B);
}

static void feed_ignore_st(AnsiParser *parser, unsigned char ch) {
  if (ch == 0x07) {
    parser->state = PARSER_STATE_GROUND;
    parser->osc_length = 0;
    return;
  }
  if (ch == 0x18 || ch == 0x1A) {
    parser->state = PARSER_STATE_GROUND;
    parser->osc_length = 0;
    utf8_reset(parser);
    return;
  }
  if (ch == 0x1B) {
    parser->osc_length = 1;
    return;
  }
  if (ch == '\\' && parser->osc_length == 1) {
    parser->state = PARSER_STATE_GROUND;
    parser->osc_length = 0;
    return;
  }
  parser->osc_length = 0;
}

static void feed_charset(AnsiParser *parser, unsigned char ch) {
  if (parser->charset_designating == '(') {
    parser->charset_g0 = (char)ch;
  } else {
    parser->charset_g1 = (char)ch;
  }
  parser->state = PARSER_STATE_GROUND;
}

void ansi_parser_feed(AnsiParser *parser, const char *data, size_t length,
                      TerminalBuffer *tb) {
  if (!parser || !data || !tb) {
    return;
  }

  /* Hot path: do not log per byte, even at TRACE. */
  for (size_t i = 0; i < length; i++) {
    unsigned char ch = (unsigned char)data[i];

    if (feed_utf8_continue(parser, tb, ch)) {
      continue;
    }

    switch (parser->state) {
    case PARSER_STATE_GROUND:
      feed_ground(parser, tb, ch);
      break;
    case PARSER_STATE_ESCAPE:
      feed_escape(parser, tb, ch);
      break;
    case PARSER_STATE_CSI:
      feed_csi_byte(parser, tb, ch, false);
      break;
    case PARSER_STATE_CSI_PARAM:
      feed_csi_byte(parser, tb, ch, true);
      break;
    case PARSER_STATE_OSC:
      feed_osc(parser, tb, ch);
      break;
    case PARSER_STATE_CHARSET:
      feed_charset(parser, ch);
      break;
    case PARSER_STATE_IGNORE_ST:
      feed_ignore_st(parser, ch);
      break;
    }
  }
}

ParserState ansi_parser_get_state(const AnsiParser *parser) {
  return parser ? parser->state : PARSER_STATE_GROUND;
}

size_t ansi_parser_get_param_count(const AnsiParser *parser) {
  return parser ? parser->param_count : 0;
}

void ansi_parser_get_modes(const AnsiParser *parser, AnsiParserModes *modes) {
  if (!modes)
    return;

  modes->keypad_application_mode = false;
  modes->cursor_keys_application_mode = false;
  modes->bracketed_paste_mode = false;
  modes->mouse_report_mode = 0;
  modes->mouse_utf8_mode = false;
  modes->mouse_sgr_mode = false;
  modes->alternate_scroll_mode = true;
  modes->focus_event_mode = false;
  modes->synchronized_output = false;

  if (!parser)
    return;

  modes->keypad_application_mode = parser->keypad_application_mode;
  modes->cursor_keys_application_mode = parser->cursor_keys_application_mode;
  modes->bracketed_paste_mode = parser->bracketed_paste_mode;
  modes->mouse_report_mode = parser->mouse_report_mode;
  modes->mouse_utf8_mode = parser->mouse_utf8_mode;
  modes->mouse_sgr_mode = parser->mouse_sgr_mode;
  modes->alternate_scroll_mode = parser->alternate_scroll_mode;
  modes->focus_event_mode = parser->focus_event_mode;
  modes->synchronized_output = parser->synchronized_output;
}

bool ansi_parser_is_synchronized(const AnsiParser *parser) {
  return parser && parser->synchronized_output;
}

uint32_t ansi_parser_sync_generation(const AnsiParser *parser) {
  return parser ? parser->sync_generation : 0;
}

bool ansi_parser_sync_should_present(bool synchronized, uint32_t now_ms,
                                     uint32_t started_ms, uint32_t timeout_ms) {
  if (!synchronized) {
    return true;
  }
  return (now_ms - started_ms) >= timeout_ms;
}

uint32_t ansi_parser_sync_started_ms(uint32_t generation,
                                     uint32_t seen_generation, uint32_t now_ms,
                                     uint32_t started_ms) {
  return generation != seen_generation ? now_ms : started_ms;
}
