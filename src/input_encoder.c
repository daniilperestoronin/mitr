/**
 * input_encoder.c - Terminal input encoding implementation
 */

#include "input_encoder.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void input_encoder_modes_from_parser(InputEncoderModes *modes,
                                     const AnsiParser *parser) {
  if (!modes)
    return;

  modes->cursor_keys_application = false;
  modes->keypad_application = false;
  modes->bracketed_paste = false;
  modes->mouse_report_mode = 0;
  modes->mouse_utf8 = false;
  modes->mouse_sgr = false;
  modes->alternate_scroll = true;

  if (!parser)
    return;

  AnsiParserModes pm;
  ansi_parser_get_modes(parser, &pm);
  modes->cursor_keys_application = pm.cursor_keys_application_mode;
  modes->keypad_application = pm.keypad_application_mode;
  modes->bracketed_paste = pm.bracketed_paste_mode;
  modes->mouse_report_mode = pm.mouse_report_mode;
  modes->mouse_utf8 = pm.mouse_utf8_mode;
  modes->mouse_sgr = pm.mouse_sgr_mode;
  modes->alternate_scroll = pm.alternate_scroll_mode;
}

static size_t write_str(char *out, size_t out_size, const char *s) {
  size_t n = strlen(s);
  if (!out || n >= out_size)
    return 0;
  memcpy(out, s, n);
  out[n] = '\0';
  return n;
}

static int xterm_mod_param(InputKeyMods mods) {
  return 1 + (mods.shift ? 1 : 0) + (mods.alt ? 2 : 0) + (mods.ctrl ? 4 : 0);
}

/* ESC [ p1 ; n FINAL — stack snprintf, then write_str */
static size_t write_mod_csi(char *out, size_t out_size, int p1, int n,
                            char final) {
  char tmp[32];
  int written = snprintf(tmp, sizeof(tmp), "\x1b[%d;%d%c", p1, n, final);
  if (written < 0)
    return 0;
  return write_str(out, out_size, tmp);
}

static size_t encode_key_unmodified(const InputEncoderModes *modes,
                                    InputKey key, char *out, size_t out_size) {
  bool app_cursor = modes->cursor_keys_application;
  bool app_keypad = modes->keypad_application;

  switch (key) {
  case INPUT_KEY_UP:
    return write_str(out, out_size, app_cursor ? "\x1bOA" : "\x1b[A");
  case INPUT_KEY_DOWN:
    return write_str(out, out_size, app_cursor ? "\x1bOB" : "\x1b[B");
  case INPUT_KEY_RIGHT:
    return write_str(out, out_size, app_cursor ? "\x1bOC" : "\x1b[C");
  case INPUT_KEY_LEFT:
    return write_str(out, out_size, app_cursor ? "\x1bOD" : "\x1b[D");
  case INPUT_KEY_HOME:
    return write_str(out, out_size, app_cursor ? "\x1bOH" : "\x1b[H");
  case INPUT_KEY_END:
    return write_str(out, out_size, app_cursor ? "\x1bOF" : "\x1b[F");
  case INPUT_KEY_RETURN:
  case INPUT_KEY_KP_ENTER:
    return write_str(out, out_size,
                     app_keypad && key == INPUT_KEY_KP_ENTER ? "\x1bOM" : "\r");
  case INPUT_KEY_BACKSPACE:
    return write_str(out, out_size, "\x7f");
  case INPUT_KEY_TAB:
    return write_str(out, out_size, "\t");
  case INPUT_KEY_ESCAPE:
    return write_str(out, out_size, "\x1b");
  case INPUT_KEY_INSERT:
    return write_str(out, out_size, "\x1b[2~");
  case INPUT_KEY_DELETE:
    return write_str(out, out_size, "\x1b[3~");
  case INPUT_KEY_PAGEUP:
    return write_str(out, out_size, "\x1b[5~");
  case INPUT_KEY_PAGEDOWN:
    return write_str(out, out_size, "\x1b[6~");
  case INPUT_KEY_F1:
    return write_str(out, out_size, "\x1bOP");
  case INPUT_KEY_F2:
    return write_str(out, out_size, "\x1bOQ");
  case INPUT_KEY_F3:
    return write_str(out, out_size, "\x1bOR");
  case INPUT_KEY_F4:
    return write_str(out, out_size, "\x1bOS");
  case INPUT_KEY_F5:
    return write_str(out, out_size, "\x1b[15~");
  case INPUT_KEY_F6:
    return write_str(out, out_size, "\x1b[17~");
  case INPUT_KEY_F7:
    return write_str(out, out_size, "\x1b[18~");
  case INPUT_KEY_F8:
    return write_str(out, out_size, "\x1b[19~");
  case INPUT_KEY_F9:
    return write_str(out, out_size, "\x1b[20~");
  case INPUT_KEY_F10:
    return write_str(out, out_size, "\x1b[21~");
  case INPUT_KEY_F11:
    return write_str(out, out_size, "\x1b[23~");
  case INPUT_KEY_F12:
    return write_str(out, out_size, "\x1b[24~");
  case INPUT_KEY_KP_0:
    return write_str(out, out_size, app_keypad ? "\x1bOp" : "0");
  case INPUT_KEY_KP_1:
    return write_str(out, out_size, app_keypad ? "\x1bOq" : "1");
  case INPUT_KEY_KP_2:
    return write_str(out, out_size, app_keypad ? "\x1bOr" : "2");
  case INPUT_KEY_KP_3:
    return write_str(out, out_size, app_keypad ? "\x1bOs" : "3");
  case INPUT_KEY_KP_4:
    return write_str(out, out_size, app_keypad ? "\x1bOt" : "4");
  case INPUT_KEY_KP_5:
    return write_str(out, out_size, app_keypad ? "\x1bOu" : "5");
  case INPUT_KEY_KP_6:
    return write_str(out, out_size, app_keypad ? "\x1bOv" : "6");
  case INPUT_KEY_KP_7:
    return write_str(out, out_size, app_keypad ? "\x1bOw" : "7");
  case INPUT_KEY_KP_8:
    return write_str(out, out_size, app_keypad ? "\x1bOx" : "8");
  case INPUT_KEY_KP_9:
    return write_str(out, out_size, app_keypad ? "\x1bOy" : "9");
  case INPUT_KEY_KP_PLUS:
    return write_str(out, out_size, app_keypad ? "\x1bOk" : "+");
  case INPUT_KEY_KP_MINUS:
    return write_str(out, out_size, app_keypad ? "\x1bOm" : "-");
  case INPUT_KEY_KP_MULTIPLY:
    return write_str(out, out_size, app_keypad ? "\x1bOj" : "*");
  case INPUT_KEY_KP_DIVIDE:
    return write_str(out, out_size, app_keypad ? "\x1bOo" : "/");
  case INPUT_KEY_KP_PERIOD:
    return write_str(out, out_size, app_keypad ? "\x1bOn" : ".");
  default:
    return 0;
  }
}

size_t input_encoder_encode_key_ex(const InputEncoderModes *modes, InputKey key,
                                   InputKeyMods mods, char *out,
                                   size_t out_size) {
  if (!modes || !out || out_size == 0)
    return 0;

  int n = xterm_mod_param(mods);
  if (n > 1) {
    switch (key) {
    case INPUT_KEY_UP:
      return write_mod_csi(out, out_size, 1, n, 'A');
    case INPUT_KEY_DOWN:
      return write_mod_csi(out, out_size, 1, n, 'B');
    case INPUT_KEY_RIGHT:
      return write_mod_csi(out, out_size, 1, n, 'C');
    case INPUT_KEY_LEFT:
      return write_mod_csi(out, out_size, 1, n, 'D');
    case INPUT_KEY_HOME:
      return write_mod_csi(out, out_size, 1, n, 'H');
    case INPUT_KEY_END:
      return write_mod_csi(out, out_size, 1, n, 'F');
    case INPUT_KEY_INSERT:
      return write_mod_csi(out, out_size, 2, n, '~');
    case INPUT_KEY_DELETE:
      return write_mod_csi(out, out_size, 3, n, '~');
    case INPUT_KEY_PAGEUP:
      return write_mod_csi(out, out_size, 5, n, '~');
    case INPUT_KEY_PAGEDOWN:
      return write_mod_csi(out, out_size, 6, n, '~');
    case INPUT_KEY_F1:
      return write_mod_csi(out, out_size, 1, n, 'P');
    case INPUT_KEY_F2:
      return write_mod_csi(out, out_size, 1, n, 'Q');
    case INPUT_KEY_F3:
      return write_mod_csi(out, out_size, 1, n, 'R');
    case INPUT_KEY_F4:
      return write_mod_csi(out, out_size, 1, n, 'S');
    case INPUT_KEY_F5:
      return write_mod_csi(out, out_size, 15, n, '~');
    case INPUT_KEY_F6:
      return write_mod_csi(out, out_size, 17, n, '~');
    case INPUT_KEY_F7:
      return write_mod_csi(out, out_size, 18, n, '~');
    case INPUT_KEY_F8:
      return write_mod_csi(out, out_size, 19, n, '~');
    case INPUT_KEY_F9:
      return write_mod_csi(out, out_size, 20, n, '~');
    case INPUT_KEY_F10:
      return write_mod_csi(out, out_size, 21, n, '~');
    case INPUT_KEY_F11:
      return write_mod_csi(out, out_size, 23, n, '~');
    case INPUT_KEY_F12:
      return write_mod_csi(out, out_size, 24, n, '~');
    default:
      break;
    }
  }

  return encode_key_unmodified(modes, key, out, out_size);
}

size_t input_encoder_encode_key(const InputEncoderModes *modes, InputKey key,
                                char *out, size_t out_size) {
  InputKeyMods mods = {false, false, false};
  return input_encoder_encode_key_ex(modes, key, mods, out, out_size);
}

size_t input_encoder_encode_ctrl(char letter, char *out, size_t out_size) {
  if (!out || out_size < 2)
    return 0;
  char c = letter;
  if (c >= 'A' && c <= 'Z')
    c = (char)(c - 'A' + 'a');
  if (c < 'a' || c > 'z')
    return 0;
  out[0] = (char)(c - 'a' + 1);
  out[1] = '\0';
  return 1;
}

size_t input_encoder_encode_alt_char(char letter, char *out, size_t out_size) {
  if (!out || out_size < 3)
    return 0;
  char c = letter;
  if (c >= 'A' && c <= 'Z')
    c = (char)(c - 'A' + 'a');
  if (c < 'a' || c > 'z')
    return 0;
  out[0] = '\x1b';
  out[1] = c;
  out[2] = '\0';
  return 2;
}

size_t input_encoder_paste_prefix(const InputEncoderModes *modes, char *out,
                                  size_t out_size) {
  if (!modes || !modes->bracketed_paste)
    return 0;
  return write_str(out, out_size, "\x1b[200~");
}

size_t input_encoder_paste_suffix(const InputEncoderModes *modes, char *out,
                                  size_t out_size) {
  if (!modes || !modes->bracketed_paste)
    return 0;
  return write_str(out, out_size, "\x1b[201~");
}

size_t input_encoder_encode_paste(const InputEncoderModes *modes,
                                  const char *text, size_t text_len, char *out,
                                  size_t out_size) {
  if (!modes || !text || !out)
    return 0;

  char prefix[16];
  char suffix[16];
  size_t pre = input_encoder_paste_prefix(modes, prefix, sizeof(prefix));
  size_t suf = input_encoder_paste_suffix(modes, suffix, sizeof(suffix));
  size_t need = pre + text_len + suf;
  if (need >= out_size)
    return 0;

  size_t pos = 0;
  if (pre > 0) {
    memcpy(out + pos, prefix, pre);
    pos += pre;
  }
  memcpy(out + pos, text, text_len);
  pos += text_len;
  if (suf > 0) {
    memcpy(out + pos, suffix, suf);
    pos += suf;
  }
  out[pos] = '\0';
  return pos;
}

static unsigned input_mouse_modifier_bits(InputKeyMods mods) {
  unsigned bits = 0;
  if (mods.shift) {
    bits |= 4u;
  }
  if (mods.alt) {
    bits |= 8u;
  }
  if (mods.ctrl) {
    bits |= 16u;
  }
  return bits;
}

static size_t input_mouse_encode_utf8_coord(unsigned value, char *out,
                                            size_t out_size) {
  if (value < 0x80u) {
    if (out_size < 1) {
      return 0;
    }
    out[0] = (char)value;
    return 1;
  }
  if (value <= 0x7ffu && out_size >= 2) {
    out[0] = (char)(0xc0u | (value >> 6u));
    out[1] = (char)(0x80u | (value & 0x3fu));
    return 2;
  }
  return 0;
}

size_t input_encoder_encode_mouse(const InputEncoderModes *modes,
                                  InputMouseAction action, int button,
                                  InputKeyMods mods, size_t row, size_t col,
                                  char *out, size_t out_size) {
  if (!modes || !out || modes->mouse_report_mode == 0 || button < 0)
    return 0;

  /* Motion reporting only in cell-motion mode (1002), except releases */
  if (action == INPUT_MOUSE_MOVE && modes->mouse_report_mode < 1002)
    return 0;

  /* Coordinates are 1-based for both protocols */
  unsigned modifier_bits = input_mouse_modifier_bits(mods);
  unsigned cb = (unsigned)button + modifier_bits;
  if (action == INPUT_MOUSE_RELEASE && !modes->mouse_sgr)
    cb = 3u + modifier_bits;
  if (action == INPUT_MOUSE_MOVE)
    cb |= 32u;

  unsigned x = (unsigned)col + 1u;
  unsigned y = (unsigned)row + 1u;

  if (modes->mouse_sgr) {
    char final = (action == INPUT_MOUSE_RELEASE) ? 'm' : 'M';
    int n = snprintf(out, out_size, "\x1b[<%u;%u;%u%c", cb, x, y, final);
    if (n < 0 || (size_t)n >= out_size)
      return 0;
    return (size_t)n;
  }

  /* X10 / normal tracking: ESC [ M Cb Cx Cy with +32 encoding */
  if (out_size < 6 || cb > 223u)
    return 0;
  if ((!modes->mouse_utf8 && (x > 223u || y > 223u)) ||
      (modes->mouse_utf8 && (col >= 2015u || row >= 2015u)))
    return 0;
  out[0] = '\x1b';
  out[1] = '[';
  out[2] = 'M';
  out[3] = (char)(cb + 32u);
  if (!modes->mouse_utf8) {
    out[4] = (char)(x + 32u);
    out[5] = (char)(y + 32u);
    if (out_size > 6)
      out[6] = '\0';
    return 6;
  }

  size_t pos = 4;
  size_t n = input_mouse_encode_utf8_coord(x + 32u, out + pos, out_size - pos);
  if (n == 0) {
    return 0;
  }
  pos += n;
  n = input_mouse_encode_utf8_coord(y + 32u, out + pos, out_size - pos);
  if (n == 0) {
    return 0;
  }
  pos += n;
  if (pos < out_size) {
    out[pos] = '\0';
  }
  return pos;
}

bool input_encoder_mouse_motion_report(int mouse_report_mode, bool cell_changed,
                                       InputMouseButtons buttons,
                                       int *button_out) {
  int button;

  if (!cell_changed || !button_out || mouse_report_mode < 1002) {
    return false;
  }
  if (buttons.left) {
    button = 0;
  } else if (buttons.middle) {
    button = 1;
  } else if (buttons.right) {
    button = 2;
  } else {
    if (mouse_report_mode != 1003) {
      return false;
    }
    button = 3;
  }

  *button_out = button;
  return true;
}

bool input_encoder_mouse_motion_cell_changed(InputMouseMotionState *state,
                                             size_t row, size_t col) {
  bool changed;

  if (!state) {
    return false;
  }
  changed = !state->cell_valid || state->row != row || state->col != col;
  state->cell_valid = true;
  state->row = row;
  state->col = col;
  return changed;
}

static int mouse_wheel_accumulate_axis(double *carry, double delta) {
  double total;
  double whole;

  if (!carry || !isfinite(delta)) {
    if (carry) {
      *carry = 0.0;
    }
    return 0;
  }
  if ((*carry > 0.0 && delta < 0.0) || (*carry < 0.0 && delta > 0.0)) {
    *carry = 0.0;
  }
  total = *carry + delta;
  if (!isfinite(total)) {
    *carry = 0.0;
    return delta > 0.0 ? 30 : -30;
  }

  whole = trunc(total);
  *carry = total - whole;
  if (whole > 30.0) {
    return 30;
  }
  if (whole < -30.0) {
    return -30;
  }
  return (int)whole;
}

InputMouseWheelSteps
input_encoder_mouse_wheel_accumulate(InputMouseWheelState *state,
                                     InputMouseWheelRoute route, double delta_x,
                                     double delta_y, bool flipped) {
  InputMouseWheelSteps steps = {0};

  if (!state) {
    return steps;
  }
  if (!state->route_valid || state->route != route) {
    state->x = 0.0;
    state->y = 0.0;
    state->route = route;
    state->route_valid = true;
  }
  if (flipped) {
    delta_x = -delta_x;
    delta_y = -delta_y;
  }
  if (!isfinite(delta_x)) {
    state->x = 0.0;
    delta_x = 0.0;
  }
  if (!isfinite(delta_y)) {
    state->y = 0.0;
    delta_y = 0.0;
  }

  /* Trackpads commonly add weak movement on the perpendicular axis. The
   * dominant axis wins each event; vertical wins exact ties. */
  if (fabs(delta_y) >= fabs(delta_x)) {
    state->x = 0.0;
    steps.y = mouse_wheel_accumulate_axis(&state->y, delta_y);
  } else {
    state->y = 0.0;
    steps.x = mouse_wheel_accumulate_axis(&state->x, delta_x);
  }
  return steps;
}

int input_encoder_mouse_wheel_button(bool horizontal, int direction) {
  if (direction == 0) {
    return -1;
  }
  if (horizontal) {
    return direction < 0 ? 66 : 67;
  }
  return direction > 0 ? 64 : 65;
}

InputMouseWheelRoute input_encoder_mouse_wheel_route(bool search_active,
                                                     bool shift,
                                                     int mouse_report_mode,
                                                     bool alt_screen,
                                                     bool alternate_scroll) {
  if (search_active || shift) {
    return INPUT_MOUSE_WHEEL_LOCAL;
  }
  if (mouse_report_mode != 0) {
    return INPUT_MOUSE_WHEEL_REPORT;
  }
  if (alt_screen) {
    return alternate_scroll ? INPUT_MOUSE_WHEEL_ALTERNATE
                            : INPUT_MOUSE_WHEEL_NONE;
  }
  return INPUT_MOUSE_WHEEL_LOCAL;
}

void input_encoder_mouse_press_record(InputMousePressState *state, int button,
                                      const InputEncoderModes *modes,
                                      bool delivered) {
  if (!state || !modes || !delivered || button < 0 || button > 2) {
    return;
  }
  state->outstanding[button] = true;
  state->modes[button] = *modes;
}

bool input_encoder_mouse_press_take(InputMousePressState *state, int button,
                                    InputEncoderModes *modes_out) {
  if (!state || button < 0 || button > 2 || !state->outstanding[button]) {
    return false;
  }
  if (modes_out) {
    *modes_out = state->modes[button];
  }
  state->outstanding[button] = false;
  return true;
}

bool input_encoder_mouse_press_pending(const InputMousePressState *state) {
  if (!state) {
    return false;
  }
  return state->outstanding[0] || state->outstanding[1] ||
         state->outstanding[2];
}

InputMouseButtons
input_encoder_mouse_press_filter_buttons(const InputMousePressState *state,
                                         InputMouseButtons physical) {
  if (!state) {
    return (InputMouseButtons){0};
  }
  return (InputMouseButtons){
      .left = physical.left && state->outstanding[0],
      .middle = physical.middle && state->outstanding[1],
      .right = physical.right && state->outstanding[2],
  };
}

bool input_encoder_local_mouse(int mouse_report_mode, bool shift) {
  return mouse_report_mode == 0 || shift;
}

bool input_encoder_mouse_up_to_pty(bool pty_press_outstanding) {
  return pty_press_outstanding;
}

size_t input_encoder_encode_focus_in(char *out, size_t out_size) {
  return write_str(out, out_size, "\x1b[I");
}

size_t input_encoder_encode_focus_out(char *out, size_t out_size) {
  return write_str(out, out_size, "\x1b[O");
}
