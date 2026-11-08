/**
 * input_encoder.h - Terminal input encoding (keys, paste, mouse)
 *
 * Encodes host input into bytes for the PTY according to parser modes
 * (DECCKM, DECKPAM, bracketed paste, mouse 1000/1002/1003/1005/1006/1007).
 * No SDL dependency — suitable for unit tests.
 */

#ifndef MITR_INPUT_ENCODER_H
#define MITR_INPUT_ENCODER_H

#include "ansi_parser.h"
#include "common.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
  INPUT_KEY_UP,
  INPUT_KEY_DOWN,
  INPUT_KEY_RIGHT,
  INPUT_KEY_LEFT,
  INPUT_KEY_HOME,
  INPUT_KEY_END,
  INPUT_KEY_RETURN,
  INPUT_KEY_BACKSPACE,
  INPUT_KEY_TAB,
  INPUT_KEY_ESCAPE,
  INPUT_KEY_INSERT,
  INPUT_KEY_DELETE,
  INPUT_KEY_PAGEUP,
  INPUT_KEY_PAGEDOWN,
  INPUT_KEY_F1,
  INPUT_KEY_F2,
  INPUT_KEY_F3,
  INPUT_KEY_F4,
  INPUT_KEY_F5,
  INPUT_KEY_F6,
  INPUT_KEY_F7,
  INPUT_KEY_F8,
  INPUT_KEY_F9,
  INPUT_KEY_F10,
  INPUT_KEY_F11,
  INPUT_KEY_F12,
  INPUT_KEY_KP_0,
  INPUT_KEY_KP_1,
  INPUT_KEY_KP_2,
  INPUT_KEY_KP_3,
  INPUT_KEY_KP_4,
  INPUT_KEY_KP_5,
  INPUT_KEY_KP_6,
  INPUT_KEY_KP_7,
  INPUT_KEY_KP_8,
  INPUT_KEY_KP_9,
  INPUT_KEY_KP_ENTER,
  INPUT_KEY_KP_PLUS,
  INPUT_KEY_KP_MINUS,
  INPUT_KEY_KP_MULTIPLY,
  INPUT_KEY_KP_DIVIDE,
  INPUT_KEY_KP_PERIOD
} InputKey;

typedef enum {
  INPUT_MOUSE_PRESS = 0,
  INPUT_MOUSE_RELEASE = 1,
  INPUT_MOUSE_MOVE = 2
} InputMouseAction;

typedef struct {
  bool cursor_keys_application; /* DECCKM */
  bool keypad_application;      /* DECKPAM */
  bool bracketed_paste;
  int mouse_report_mode; /* 0, 1000, 1002, 1003 */
  bool mouse_utf8;       /* 1005 */
  bool mouse_sgr;        /* 1006 */
  bool alternate_scroll; /* 1007 */
} InputEncoderModes;

typedef struct {
  bool shift;
  bool alt;
  bool ctrl;
} InputKeyMods;

typedef struct {
  bool left;
  bool middle;
  bool right;
} InputMouseButtons;

typedef struct {
  bool cell_valid;
  size_t row;
  size_t col;
} InputMouseMotionState;

typedef struct {
  bool outstanding[3];
  InputEncoderModes modes[3];
} InputMousePressState;

typedef enum {
  INPUT_MOUSE_WHEEL_NONE = 0,
  INPUT_MOUSE_WHEEL_LOCAL,
  INPUT_MOUSE_WHEEL_REPORT,
  INPUT_MOUSE_WHEEL_ALTERNATE
} InputMouseWheelRoute;

typedef struct {
  double x;
  double y;
  InputMouseWheelRoute route;
  bool route_valid;
} InputMouseWheelState;

typedef struct {
  int x;
  int y;
} InputMouseWheelSteps;

/**
 * Snapshot mode flags from an AnsiParser (safe if parser is nullptr →
 * defaults).
 */
void input_encoder_modes_from_parser(InputEncoderModes *modes,
                                     const AnsiParser *parser);

/**
 * Encode a special key into @p out (no modifiers).
 * @return Bytes written, or 0 if unrecognized / buffer too small
 */
size_t input_encoder_encode_key(const InputEncoderModes *modes, InputKey key,
                                char *out, size_t out_size);

/**
 * Encode a special key with xterm CSI modifier parameters
 * (n = 1 + shift + alt*2 + ctrl*4). Zero mods match encode_key.
 * Return/Tab/Escape/Backspace and keypad keep the unmodified sequence.
 * @return Bytes written, or 0 if unrecognized / buffer too small
 */
size_t input_encoder_encode_key_ex(const InputEncoderModes *modes, InputKey key,
                                   InputKeyMods mods, char *out,
                                   size_t out_size);

/**
 * Encode Ctrl+letter (a-z) as a C0 control byte.
 */
size_t input_encoder_encode_ctrl(char letter, char *out, size_t out_size);

/**
 * Encode Alt+letter (A–Z / a–z) as ESC + lowercase letter.
 * @return 2 on success, or 0 if not a letter / buffer too small
 */
size_t input_encoder_encode_alt_char(char letter, char *out, size_t out_size);

/**
 * Encode paste text, wrapping with bracketed-paste markers when enabled.
 * Writes into @p out; if paste is larger than out_size, returns 0.
 * For large pastes prefer input_encoder_paste_prefix/suffix + raw write.
 */
size_t input_encoder_encode_paste(const InputEncoderModes *modes,
                                  const char *text, size_t text_len, char *out,
                                  size_t out_size);

size_t input_encoder_paste_prefix(const InputEncoderModes *modes, char *out,
                                  size_t out_size);
size_t input_encoder_paste_suffix(const InputEncoderModes *modes, char *out,
                                  size_t out_size);

/**
 * Encode a mouse event from zero-based cells into one-based wire coordinates.
 * @param button 0=left, 1=middle, 2=right, 3=no button, 64..67=wheel
 * @param mods Shift/Alt/Ctrl modifier state encoded in Cb
 * @return Bytes written, or 0 if mouse reporting disabled / buffer too small
 */
size_t input_encoder_encode_mouse(const InputEncoderModes *modes,
                                  InputMouseAction action, int button,
                                  InputKeyMods mods, size_t row, size_t col,
                                  char *out, size_t out_size);

/** Select the xterm motion button code when this cell should be reported. */
bool input_encoder_mouse_motion_report(int mouse_report_mode, bool cell_changed,
                                       InputMouseButtons buttons,
                                       int *button_out);

/** Update the last mouse cell and report whether it changed. */
bool input_encoder_mouse_motion_cell_changed(InputMouseMotionState *state,
                                             size_t row, size_t col);

/** Accumulate route-local, dominant-axis deltas into bounded whole steps. */
InputMouseWheelSteps
input_encoder_mouse_wheel_accumulate(InputMouseWheelState *state,
                                     InputMouseWheelRoute route, double delta_x,
                                     double delta_y, bool flipped);

/** Map a signed wheel step to xterm buttons 64..67, or -1 for zero. */
int input_encoder_mouse_wheel_button(bool horizontal, int direction);

/** Select host scrollback, mouse reports, alternate-scroll keys, or no-op. */
InputMouseWheelRoute input_encoder_mouse_wheel_route(bool search_active,
                                                     bool shift,
                                                     int mouse_report_mode,
                                                     bool alt_screen,
                                                     bool alternate_scroll);

/** Record/take successfully delivered button presses and their wire modes. */
void input_encoder_mouse_press_record(InputMousePressState *state, int button,
                                      const InputEncoderModes *modes,
                                      bool delivered);
bool input_encoder_mouse_press_take(InputMousePressState *state, int button,
                                    InputEncoderModes *modes_out);
bool input_encoder_mouse_press_pending(const InputMousePressState *state);

/** Keep only physical buttons whose PTY PRESS was successfully delivered. */
InputMouseButtons
input_encoder_mouse_press_filter_buttons(const InputMousePressState *state,
                                         InputMouseButtons physical);

/**
 * True when the host should handle the mouse locally (no PTY reports).
 * Reporting off, or Shift held (Alacritty-style override for selection).
 */
bool input_encoder_local_mouse(int mouse_report_mode, bool shift);

/**
 * True when button-up must be encoded to the PTY.
 * Only a successfully delivered PRESS for this button permits a matching
 * RELEASE. This also prevents a late host button-up after focus loss from
 * generating an unmatched PTY report.
 */
bool input_encoder_mouse_up_to_pty(bool pty_press_outstanding);

/**
 * Encode focus-in (CSI I) / focus-out (CSI O) for DECSET 1004.
 */
size_t input_encoder_encode_focus_in(char *out, size_t out_size);
size_t input_encoder_encode_focus_out(char *out, size_t out_size);

#endif /* MITR_INPUT_ENCODER_H */
