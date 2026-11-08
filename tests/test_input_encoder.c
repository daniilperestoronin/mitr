/**
 * test_input_encoder.c - Unit tests for terminal input encoding
 */

#include "ansi_parser.h"
#include "input_encoder.h"
#include "pty_manager.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include <string.h>

TEST(input_encoder_arrow_keys_normal_mode) {
  InputEncoderModes modes = {0};
  char buf[16];

  ASSERT_EQ(input_encoder_encode_key(&modes, INPUT_KEY_UP, buf, sizeof(buf)),
            3);
  ASSERT_TRUE(memcmp(buf, "\x1b[A", 3) == 0);

  ASSERT_EQ(input_encoder_encode_key(&modes, INPUT_KEY_LEFT, buf, sizeof(buf)),
            3);
  ASSERT_TRUE(memcmp(buf, "\x1b[D", 3) == 0);
}

TEST(input_encoder_arrow_keys_application_mode) {
  InputEncoderModes modes = {.cursor_keys_application = true};
  char buf[16];

  ASSERT_EQ(input_encoder_encode_key(&modes, INPUT_KEY_UP, buf, sizeof(buf)),
            3);
  ASSERT_TRUE(memcmp(buf, "\x1bOA", 3) == 0);

  ASSERT_EQ(input_encoder_encode_key(&modes, INPUT_KEY_RIGHT, buf, sizeof(buf)),
            3);
  ASSERT_TRUE(memcmp(buf, "\x1bOC", 3) == 0);
}

TEST(input_encoder_keypad_application_mode) {
  InputEncoderModes modes = {.keypad_application = true};
  char buf[16];

  ASSERT_EQ(input_encoder_encode_key(&modes, INPUT_KEY_KP_5, buf, sizeof(buf)),
            3);
  ASSERT_TRUE(memcmp(buf, "\x1bOu", 3) == 0);

  ASSERT_EQ(
      input_encoder_encode_key(&modes, INPUT_KEY_KP_ENTER, buf, sizeof(buf)),
      3);
  ASSERT_TRUE(memcmp(buf, "\x1bOM", 3) == 0);
}

TEST(input_encoder_f1_ss3) {
  InputEncoderModes modes = {0};
  char buf[16];

  ASSERT_EQ(input_encoder_encode_key(&modes, INPUT_KEY_F1, buf, sizeof(buf)),
            3);
  ASSERT_TRUE(memcmp(buf, "\x1bOP", 3) == 0);
}

TEST(input_encoder_f5_csi) {
  InputEncoderModes modes = {0};
  char buf[16];

  ASSERT_EQ(input_encoder_encode_key(&modes, INPUT_KEY_F5, buf, sizeof(buf)),
            5);
  ASSERT_TRUE(memcmp(buf, "\x1b[15~", 5) == 0);
}

TEST(input_encoder_f12_csi) {
  InputEncoderModes modes = {0};
  char buf[16];

  ASSERT_EQ(input_encoder_encode_key(&modes, INPUT_KEY_F12, buf, sizeof(buf)),
            5);
  ASSERT_TRUE(memcmp(buf, "\x1b[24~", 5) == 0);
}

TEST(input_encoder_shift_up) {
  InputEncoderModes modes = {0};
  InputKeyMods mods = {.shift = true};
  char buf[16];

  ASSERT_EQ(
      input_encoder_encode_key_ex(&modes, INPUT_KEY_UP, mods, buf, sizeof(buf)),
      6);
  ASSERT_STR_EQ(buf, "\x1b[1;2A");

  /* Modified arrows stay CSI even when DECCKM application mode is on. */
  modes.cursor_keys_application = true;
  ASSERT_EQ(
      input_encoder_encode_key_ex(&modes, INPUT_KEY_UP, mods, buf, sizeof(buf)),
      6);
  ASSERT_STR_EQ(buf, "\x1b[1;2A");
}

TEST(input_encoder_ctrl_left) {
  InputEncoderModes modes = {0};
  InputKeyMods mods = {.ctrl = true};
  char buf[16];

  ASSERT_EQ(input_encoder_encode_key_ex(&modes, INPUT_KEY_LEFT, mods, buf,
                                        sizeof(buf)),
            6);
  ASSERT_STR_EQ(buf, "\x1b[1;5D");
}

TEST(input_encoder_alt_delete) {
  InputEncoderModes modes = {0};
  InputKeyMods mods = {.alt = true};
  char buf[16];

  ASSERT_EQ(input_encoder_encode_key_ex(&modes, INPUT_KEY_DELETE, mods, buf,
                                        sizeof(buf)),
            6);
  ASSERT_STR_EQ(buf, "\x1b[3;3~");
}

TEST(input_encoder_shift_f5) {
  InputEncoderModes modes = {0};
  InputKeyMods mods = {.shift = true};
  char buf[16];

  ASSERT_EQ(
      input_encoder_encode_key_ex(&modes, INPUT_KEY_F5, mods, buf, sizeof(buf)),
      7);
  ASSERT_STR_EQ(buf, "\x1b[15;2~");
}

TEST(input_encoder_shift_f1) {
  InputEncoderModes modes = {0};
  InputKeyMods mods = {.shift = true};
  char buf[16];

  ASSERT_EQ(
      input_encoder_encode_key_ex(&modes, INPUT_KEY_F1, mods, buf, sizeof(buf)),
      6);
  ASSERT_STR_EQ(buf, "\x1b[1;2P");
}

TEST(input_encoder_zero_mods_f1_and_up_unchanged) {
  InputEncoderModes modes = {0};
  InputKeyMods mods = {false, false, false};
  char buf[16];

  ASSERT_EQ(
      input_encoder_encode_key_ex(&modes, INPUT_KEY_F1, mods, buf, sizeof(buf)),
      3);
  ASSERT_STR_EQ(buf, "\x1bOP");

  ASSERT_EQ(
      input_encoder_encode_key_ex(&modes, INPUT_KEY_UP, mods, buf, sizeof(buf)),
      3);
  ASSERT_STR_EQ(buf, "\x1b[A");
}

TEST(input_encoder_focus_in_out) {
  char buf[8];

  ASSERT_EQ(input_encoder_encode_focus_in(buf, sizeof(buf)), 3);
  ASSERT_TRUE(memcmp(buf, "\x1b[I", 3) == 0);

  ASSERT_EQ(input_encoder_encode_focus_out(buf, sizeof(buf)), 3);
  ASSERT_TRUE(memcmp(buf, "\x1b[O", 3) == 0);

  ASSERT_EQ(input_encoder_encode_focus_in(buf, 3), 0);
  ASSERT_EQ(input_encoder_encode_focus_out(nullptr, 8), 0);
}

TEST(input_encoder_ctrl_letter) {
  char buf[4];
  ASSERT_EQ(input_encoder_encode_ctrl('c', buf, sizeof(buf)), 1);
  ASSERT_EQ(buf[0], 3);
  ASSERT_EQ(input_encoder_encode_ctrl('A', buf, sizeof(buf)), 1);
  ASSERT_EQ(buf[0], 1);
}

TEST(input_encoder_alt_char) {
  char buf[4];
  ASSERT_EQ(input_encoder_encode_alt_char('x', buf, sizeof(buf)), 2);
  ASSERT_STR_EQ(buf, "\x1b"
                     "x");
  ASSERT_EQ(input_encoder_encode_alt_char('A', buf, sizeof(buf)), 2);
  ASSERT_STR_EQ(buf, "\x1b"
                     "a");
  ASSERT_EQ(input_encoder_encode_alt_char('1', buf, sizeof(buf)), 0);
  ASSERT_EQ(input_encoder_encode_alt_char('z', buf, 2), 0);
}

TEST(input_encoder_bracketed_paste) {
  InputEncoderModes modes = {.bracketed_paste = true};
  char buf[64];
  size_t n = input_encoder_encode_paste(&modes, "hi", 2, buf, sizeof(buf));
  ASSERT_TRUE(n > 2);
  ASSERT_TRUE(memcmp(buf, "\x1b[200~", 6) == 0);
  ASSERT_TRUE(memcmp(buf + 6, "hi", 2) == 0);
  ASSERT_TRUE(memcmp(buf + 8, "\x1b[201~", 6) == 0);

  InputEncoderModes off = {0};
  n = input_encoder_encode_paste(&off, "hi", 2, buf, sizeof(buf));
  ASSERT_EQ(n, 2);
  ASSERT_TRUE(memcmp(buf, "hi", 2) == 0);
}

TEST(input_encoder_mouse_sgr) {
  InputEncoderModes modes = {.mouse_report_mode = 1000, .mouse_sgr = true};
  char buf[32];
  size_t n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 0,
                                        (InputKeyMods){0}, 2, 4, buf,
                                        sizeof(buf));
  ASSERT_TRUE(n > 0);
  ASSERT_STR_EQ(buf, "\x1b[<0;5;3M");

  n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_RELEASE, 0,
                                 (InputKeyMods){0}, 2, 4, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "\x1b[<0;5;3m");
}

TEST(input_encoder_mouse_x10) {
  InputEncoderModes modes = {.mouse_report_mode = 1000, .mouse_sgr = false};
  char buf[16];
  size_t n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 0,
                                        (InputKeyMods){0}, 0, 0, buf,
                                        sizeof(buf));
  ASSERT_EQ(n, 6);
  ASSERT_EQ(buf[0], '\x1b');
  ASSERT_EQ(buf[1], '[');
  ASSERT_EQ(buf[2], 'M');
  ASSERT_EQ(buf[3], (char)(32)); /* button 0 + 32 */
  ASSERT_EQ(buf[4], (char)(33)); /* col 1 + 32 */
  ASSERT_EQ(buf[5], (char)(33)); /* row 1 + 32 */
}

TEST(input_encoder_mouse_disabled) {
  InputEncoderModes modes = {0};
  char buf[32];
  ASSERT_EQ(input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 0,
                                       (InputKeyMods){0}, 0, 0, buf,
                                       sizeof(buf)),
            0);
}

TEST(input_encoder_mouse_sgr_wheel_up) {
  InputEncoderModes modes = {.mouse_report_mode = 1000, .mouse_sgr = true};
  char buf[32];
  size_t n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 64,
                                        (InputKeyMods){0}, 2, 4, buf,
                                        sizeof(buf));
  ASSERT_TRUE(n > 0);
  ASSERT_STR_EQ(buf, "\x1b[<64;5;3M");
}

TEST(input_encoder_mouse_sgr_wheel_down) {
  InputEncoderModes modes = {.mouse_report_mode = 1000, .mouse_sgr = true};
  char buf[32];
  size_t n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 65,
                                        (InputKeyMods){0}, 2, 4, buf,
                                        sizeof(buf));
  ASSERT_TRUE(n > 0);
  ASSERT_STR_EQ(buf, "\x1b[<65;5;3M");
}

TEST(input_encoder_mouse_x10_wheel_up) {
  InputEncoderModes modes = {.mouse_report_mode = 1000, .mouse_sgr = false};
  char buf[16];
  size_t n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 64,
                                        (InputKeyMods){0}, 0, 0, buf,
                                        sizeof(buf));
  ASSERT_EQ(n, 6);
  ASSERT_EQ(buf[0], '\x1b');
  ASSERT_EQ(buf[1], '[');
  ASSERT_EQ(buf[2], 'M');
  ASSERT_EQ(buf[3], (char)(64 + 32)); /* Cb = 64 + 32 */
  ASSERT_EQ(buf[4], (char)(33));
  ASSERT_EQ(buf[5], (char)(33));
}

TEST(input_encoder_mouse_wheel_disabled) {
  InputEncoderModes modes = {0};
  char buf[32];
  ASSERT_EQ(input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 64,
                                       (InputKeyMods){0}, 2, 4, buf,
                                       sizeof(buf)),
            0);
}

TEST(input_encoder_mouse_sgr_modifiers_and_motion_buttons) {
  InputEncoderModes modes = {.mouse_report_mode = 1003, .mouse_sgr = true};
  char buf[32];
  InputKeyMods all_mods = {.shift = true, .alt = true, .ctrl = true};

  size_t n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 0, all_mods,
                                        2, 4, buf, sizeof(buf));
  ASSERT_TRUE(n > 0);
  ASSERT_STR_EQ(buf, "\x1b[<28;5;3M");

  n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_MOVE, 2,
                                 (InputKeyMods){.alt = true}, 2, 4, buf,
                                 sizeof(buf));
  ASSERT_STR_EQ(buf, "\x1b[<42;5;3M");

  n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_MOVE, 3,
                                 (InputKeyMods){0}, 2, 4, buf, sizeof(buf));
  ASSERT_STR_EQ(buf, "\x1b[<35;5;3M");
}

TEST(input_encoder_mouse_legacy_release_keeps_modifiers) {
  InputEncoderModes modes = {.mouse_report_mode = 1000};
  char buf[16];

  size_t n = input_encoder_encode_mouse(
      &modes, INPUT_MOUSE_RELEASE, 0, (InputKeyMods){.ctrl = true}, 0, 0, buf,
      sizeof(buf));

  ASSERT_EQ(n, 6);
  ASSERT_EQ(buf[3], (char)(32 + 3 + 16));
}

TEST(input_encoder_mouse_utf8_coordinates) {
  InputEncoderModes modes = {.mouse_report_mode = 1000, .mouse_utf8 = true};
  char buf[16];

  size_t n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 0,
                                        (InputKeyMods){0}, 0, 95, buf,
                                        sizeof(buf));
  const unsigned char expected[] = {'\x1b', '[', 'M', 32, 0xc2, 0x80, 33};
  ASSERT_EQ(n, sizeof(expected));
  ASSERT_MEM_EQ(buf, expected, sizeof(expected));

  n = input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 0,
                                 (InputKeyMods){0}, 2014, 2014, buf,
                                 sizeof(buf));
  const unsigned char maximum[] = {'\x1b', '[',  'M', 32,
                                   0xdf,   0xbf, 0xdf, 0xbf};
  ASSERT_EQ(n, sizeof(maximum));
  ASSERT_MEM_EQ(buf, maximum, sizeof(maximum));

  ASSERT_EQ(input_encoder_encode_mouse(&modes, INPUT_MOUSE_PRESS, 0,
                                       (InputKeyMods){0}, 0, 2015, buf,
                                       sizeof(buf)),
            0);
}

TEST(input_encoder_mouse_motion_routing) {
  int button = -1;

  ASSERT_FALSE(input_encoder_mouse_motion_report(
      1000, true, (InputMouseButtons){.left = true}, &button));
  ASSERT_FALSE(input_encoder_mouse_motion_report(
      1002, false, (InputMouseButtons){.left = true}, &button));
  ASSERT_FALSE(input_encoder_mouse_motion_report(1002, true,
                                                 (InputMouseButtons){0},
                                                 &button));

  ASSERT_TRUE(input_encoder_mouse_motion_report(
      1002, true, (InputMouseButtons){.left = true}, &button));
  ASSERT_EQ(button, 0);
  ASSERT_TRUE(input_encoder_mouse_motion_report(
      1002, true, (InputMouseButtons){.middle = true}, &button));
  ASSERT_EQ(button, 1);
  ASSERT_TRUE(input_encoder_mouse_motion_report(
      1002, true, (InputMouseButtons){.right = true}, &button));
  ASSERT_EQ(button, 2);

  ASSERT_TRUE(input_encoder_mouse_motion_report(1003, true,
                                                (InputMouseButtons){0},
                                                &button));
  ASSERT_EQ(button, 3);
}

TEST(input_encoder_mouse_motion_prefers_left_then_middle) {
  int button = -1;
  InputMouseButtons all = {.left = true, .middle = true, .right = true};

  ASSERT_TRUE(input_encoder_mouse_motion_report(1002, true, all, &button));
  ASSERT_EQ(button, 0);

  all.left = false;
  ASSERT_TRUE(input_encoder_mouse_motion_report(1002, true, all, &button));
  ASSERT_EQ(button, 1);
}

TEST(input_encoder_mouse_motion_state_persists_across_updates) {
  InputMouseMotionState state = {0};

  ASSERT_TRUE(input_encoder_mouse_motion_cell_changed(&state, 4, 7));
  ASSERT_FALSE(input_encoder_mouse_motion_cell_changed(&state, 4, 7));
  ASSERT_TRUE(input_encoder_mouse_motion_cell_changed(&state, 4, 8));
  ASSERT_FALSE(input_encoder_mouse_motion_cell_changed(&state, 4, 8));
}

TEST(input_encoder_mouse_wheel_accumulates_precise_axes) {
  InputMouseWheelState state = {0};
  InputMouseWheelSteps steps;

  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.0, -0.4, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, 0);

  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.0, -0.7, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, -1);

  state = (InputMouseWheelState){0};
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.4, 0.0, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.7, 0.0, false);
  ASSERT_EQ(steps.x, 1);
  ASSERT_EQ(steps.y, 0);
}

TEST(input_encoder_mouse_wheel_flips_both_axes_and_caps_bursts) {
  InputMouseWheelState state = {0};
  InputMouseWheelSteps steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.0, -3.0, true);

  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, 3);

  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 1e300, 0.0, false);
  ASSERT_EQ(steps.x, 30);
  ASSERT_EQ(steps.y, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.0, -1e300, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, -30);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.0, 0.0, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, 0);
}

TEST(input_encoder_mouse_wheel_carry_does_not_cross_routes) {
  InputMouseWheelState state = {0};
  const InputMouseWheelRoute routes[] = {
      INPUT_MOUSE_WHEEL_LOCAL,
      INPUT_MOUSE_WHEEL_REPORT,
      INPUT_MOUSE_WHEEL_ALTERNATE,
      INPUT_MOUSE_WHEEL_NONE,
      INPUT_MOUSE_WHEEL_LOCAL,
  };

  for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
    InputMouseWheelSteps steps = input_encoder_mouse_wheel_accumulate(
        &state, routes[i], 0.0, 0.6, false);
    ASSERT_EQ(steps.x, 0);
    ASSERT_EQ(steps.y, 0);
  }
}

TEST(input_encoder_mouse_wheel_suppresses_cross_axis_noise) {
  InputMouseWheelState state = {0};
  InputMouseWheelSteps steps;

  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.3, 0.6, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.3, 0.6, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, 1);

  state = (InputMouseWheelState){0};
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, -0.6, -0.3, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, -0.6, -0.3, false);
  ASSERT_EQ(steps.x, -1);
  ASSERT_EQ(steps.y, 0);
}

TEST(input_encoder_mouse_wheel_equal_axes_prefer_vertical) {
  InputMouseWheelState state = {0};
  InputMouseWheelSteps steps;

  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.6, -0.6, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.6, -0.6, false);
  ASSERT_EQ(steps.x, 0);
  ASSERT_EQ(steps.y, -1);
}

TEST(input_encoder_mouse_wheel_dominant_axis_clears_suppressed_carry) {
  InputMouseWheelState state = {0};
  InputMouseWheelSteps steps;

  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.8, 0.0, false);
  ASSERT_EQ(steps.x, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.3, 0.6, false);
  ASSERT_EQ(steps.y, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.3, 0.0, false);
  ASSERT_EQ(steps.x, 0);

  state = (InputMouseWheelState){0};
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.0, -0.8, false);
  ASSERT_EQ(steps.y, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, -0.6, -0.3, false);
  ASSERT_EQ(steps.x, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.0, -0.3, false);
  ASSERT_EQ(steps.y, 0);
}

TEST(input_encoder_mouse_wheel_direction_change_drops_old_carry) {
  InputMouseWheelState state = {0};
  InputMouseWheelSteps steps;

  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.0, 0.9, false);
  ASSERT_EQ(steps.y, 0);
  steps = input_encoder_mouse_wheel_accumulate(
      &state, INPUT_MOUSE_WHEEL_REPORT, 0.0, -1.0, false);
  ASSERT_EQ(steps.y, -1);
}

TEST(input_encoder_mouse_wheel_maps_vertical_and_horizontal_buttons) {
  ASSERT_EQ(input_encoder_mouse_wheel_button(false, 1), 64);
  ASSERT_EQ(input_encoder_mouse_wheel_button(false, -1), 65);
  ASSERT_EQ(input_encoder_mouse_wheel_button(true, -1), 66);
  ASSERT_EQ(input_encoder_mouse_wheel_button(true, 1), 67);
}

TEST(input_encoder_mouse_wheel_route_respects_alternate_scroll_mode) {
  ASSERT_EQ(input_encoder_mouse_wheel_route(false, false, 1002, true, false),
            INPUT_MOUSE_WHEEL_REPORT);
  ASSERT_EQ(input_encoder_mouse_wheel_route(false, false, 0, true, true),
            INPUT_MOUSE_WHEEL_ALTERNATE);
  ASSERT_EQ(input_encoder_mouse_wheel_route(false, false, 0, true, false),
            INPUT_MOUSE_WHEEL_NONE);
  ASSERT_EQ(input_encoder_mouse_wheel_route(false, false, 0, false, true),
            INPUT_MOUSE_WHEEL_LOCAL);
  ASSERT_EQ(input_encoder_mouse_wheel_route(true, false, 1002, true, true),
            INPUT_MOUSE_WHEEL_LOCAL);
  ASSERT_EQ(input_encoder_mouse_wheel_route(false, true, 1002, true, true),
            INPUT_MOUSE_WHEEL_LOCAL);
}

TEST(input_encoder_mouse_press_state_records_only_delivered_reports) {
  InputMousePressState state = {0};
  InputEncoderModes modes = {.mouse_report_mode = 1003, .mouse_sgr = true};

  input_encoder_mouse_press_record(&state, 0, &modes, false);
  ASSERT_FALSE(input_encoder_mouse_press_pending(&state));

  input_encoder_mouse_press_record(&state, 0, &modes, true);
  ASSERT_TRUE(input_encoder_mouse_press_pending(&state));
}

TEST(input_encoder_mouse_press_state_restores_protocol_snapshot) {
  InputMousePressState state = {0};
  InputEncoderModes press_modes = {
      .mouse_report_mode = 1003,
      .mouse_utf8 = true,
      .alternate_scroll = true,
  };
  InputEncoderModes release_modes = {0};

  input_encoder_mouse_press_record(&state, 2, &press_modes, true);
  ASSERT_TRUE(
      input_encoder_mouse_press_take(&state, 2, &release_modes));
  ASSERT_EQ(release_modes.mouse_report_mode, 1003);
  ASSERT_TRUE(release_modes.mouse_utf8);
  ASSERT_FALSE(release_modes.mouse_sgr);
  ASSERT_FALSE(input_encoder_mouse_press_pending(&state));
}

TEST(input_encoder_mouse_press_state_tracks_buttons_and_contexts) {
  InputMousePressState first = {0};
  InputMousePressState second = {0};
  InputEncoderModes modes = {.mouse_report_mode = 1002, .mouse_sgr = true};
  InputEncoderModes saved = {0};

  input_encoder_mouse_press_record(&first, 0, &modes, true);
  input_encoder_mouse_press_record(&first, 1, &modes, true);
  ASSERT_TRUE(input_encoder_mouse_press_pending(&first));
  ASSERT_FALSE(input_encoder_mouse_press_pending(&second));

  ASSERT_TRUE(input_encoder_mouse_press_take(&first, 0, &saved));
  ASSERT_TRUE(input_encoder_mouse_press_pending(&first));
  ASSERT_TRUE(input_encoder_mouse_press_take(&first, 1, &saved));
  ASSERT_FALSE(input_encoder_mouse_press_pending(&first));
  ASSERT_FALSE(input_encoder_mouse_press_take(&first, 2, &saved));
}

TEST(input_encoder_mouse_press_filter_rejects_undelivered_buttons) {
  InputMousePressState state = {0};
  InputMouseButtons physical = {.left = true, .middle = true, .right = true};
  InputMouseButtons filtered =
      input_encoder_mouse_press_filter_buttons(&state, physical);

  ASSERT_FALSE(filtered.left);
  ASSERT_FALSE(filtered.middle);
  ASSERT_FALSE(filtered.right);
}

TEST(input_encoder_mouse_press_filter_keeps_only_outstanding_buttons) {
  InputMousePressState state = {0};
  InputEncoderModes modes = {.mouse_report_mode = 1002, .mouse_sgr = true};
  InputMouseButtons physical = {.left = true, .middle = true, .right = true};
  int button = -1;

  input_encoder_mouse_press_record(&state, 2, &modes, true);
  InputMouseButtons filtered =
      input_encoder_mouse_press_filter_buttons(&state, physical);

  ASSERT_FALSE(filtered.left);
  ASSERT_FALSE(filtered.middle);
  ASSERT_TRUE(filtered.right);
  ASSERT_TRUE(
      input_encoder_mouse_motion_report(1002, true, filtered, &button));
  ASSERT_EQ(button, 2);
}

TEST(input_encoder_mouse_press_filter_rejects_button_taken_on_focus_loss) {
  InputMousePressState state = {0};
  InputEncoderModes modes = {.mouse_report_mode = 1002, .mouse_sgr = true};
  InputEncoderModes saved = {0};
  InputMouseButtons physical = {.right = true};

  input_encoder_mouse_press_record(&state, 2, &modes, true);
  ASSERT_TRUE(input_encoder_mouse_press_take(&state, 2, &saved));
  InputMouseButtons filtered =
      input_encoder_mouse_press_filter_buttons(&state, physical);

  ASSERT_FALSE(filtered.left);
  ASSERT_FALSE(filtered.middle);
  ASSERT_FALSE(filtered.right);
}

TEST(input_encoder_mouse_press_filter_composes_with_motion_modes) {
  InputMousePressState state = {0};
  InputMouseButtons physical = {.left = true};
  InputMouseButtons filtered =
      input_encoder_mouse_press_filter_buttons(&state, physical);
  int button = -1;

  ASSERT_FALSE(
      input_encoder_mouse_motion_report(1002, true, filtered, &button));
  ASSERT_TRUE(
      input_encoder_mouse_motion_report(1003, true, filtered, &button));
  ASSERT_EQ(button, 3);
}

TEST(input_encoder_local_mouse_reporting_off) {
  ASSERT_TRUE(input_encoder_local_mouse(0, false));
}

TEST(input_encoder_local_mouse_reporting_without_shift) {
  ASSERT_FALSE(input_encoder_local_mouse(1000, false));
}

TEST(input_encoder_local_mouse_shift_overrides_1000) {
  ASSERT_TRUE(input_encoder_local_mouse(1000, true));
}

TEST(input_encoder_local_mouse_shift_overrides_1002) {
  ASSERT_TRUE(input_encoder_local_mouse(1002, true));
}

TEST(input_encoder_mouse_up_to_pty_accepts_delivered_press) {
  ASSERT_TRUE(input_encoder_mouse_up_to_pty(true));
}

TEST(input_encoder_mouse_up_to_pty_local_without_press) {
  ASSERT_FALSE(input_encoder_mouse_up_to_pty(false));
}

TEST(input_encoder_mouse_up_to_pty_rejects_failed_press) {
  InputMousePressState state = {0};
  InputEncoderModes modes = {.mouse_report_mode = 1002, .mouse_sgr = true};
  InputEncoderModes saved = {0};

  input_encoder_mouse_press_record(&state, 0, &modes, false);
  ASSERT_FALSE(input_encoder_mouse_press_take(&state, 0, &saved));
  ASSERT_FALSE(input_encoder_mouse_up_to_pty(false));
}

TEST(input_encoder_mouse_up_to_pty_rejects_press_consumed_on_focus_loss) {
  InputMousePressState state = {0};
  InputEncoderModes modes = {.mouse_report_mode = 1003, .mouse_sgr = true};
  InputEncoderModes saved = {0};

  input_encoder_mouse_press_record(&state, 2, &modes, true);
  ASSERT_TRUE(input_encoder_mouse_press_take(&state, 2, &saved));
  ASSERT_FALSE(input_encoder_mouse_press_take(&state, 2, &saved));
  ASSERT_FALSE(input_encoder_mouse_up_to_pty(false));
}

TEST(input_encoder_modes_from_parser) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(parser);
  ASSERT_NOT_NULL(tb);

  ansi_parser_feed(parser, "\x1b[?1h", 5, tb);
  ansi_parser_feed(parser, "\x1b[?2004h", 8, tb);
  ansi_parser_feed(parser, "\x1b[?1002h", 8, tb);
  ansi_parser_feed(parser, "\x1b[?1005h", 8, tb);
  ansi_parser_feed(parser, "\x1b[?1007l", 8, tb);

  InputEncoderModes modes;
  input_encoder_modes_from_parser(&modes, parser);
  ASSERT_TRUE(modes.cursor_keys_application);
  ASSERT_TRUE(modes.bracketed_paste);
  ASSERT_EQ(modes.mouse_report_mode, 1002);
  ASSERT_TRUE(modes.mouse_utf8);
  ASSERT_FALSE(modes.mouse_sgr);
  ASSERT_FALSE(modes.alternate_scroll);

  ansi_parser_feed(parser, "\x1b[?1006h", 8, tb);
  input_encoder_modes_from_parser(&modes, parser);
  ASSERT_FALSE(modes.mouse_utf8);
  ASSERT_TRUE(modes.mouse_sgr);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(input_encoder_modes_from_parser_defaults) {
  AnsiParser *parser = ansi_parser_init();
  ASSERT_NOT_NULL(parser);

  InputEncoderModes modes;
  input_encoder_modes_from_parser(&modes, parser);
  ASSERT_FALSE(modes.cursor_keys_application);
  ASSERT_FALSE(modes.keypad_application);
  ASSERT_FALSE(modes.bracketed_paste);
  ASSERT_EQ(modes.mouse_report_mode, 0);
  ASSERT_FALSE(modes.mouse_utf8);
  ASSERT_FALSE(modes.mouse_sgr);
  ASSERT_TRUE(modes.alternate_scroll);

  input_encoder_modes_from_parser(&modes, nullptr);
  ASSERT_FALSE(modes.cursor_keys_application);
  ASSERT_FALSE(modes.keypad_application);
  ASSERT_FALSE(modes.bracketed_paste);
  ASSERT_EQ(modes.mouse_report_mode, 0);
  ASSERT_FALSE(modes.mouse_utf8);
  ASSERT_FALSE(modes.mouse_sgr);
  ASSERT_TRUE(modes.alternate_scroll);

  ansi_parser_destroy(parser);
}

static size_t g_resize_rows;
static size_t g_resize_cols;
static int g_resize_calls;

static void test_resize_cb(void *userdata, size_t rows, size_t cols) {
  (void)userdata;
  g_resize_rows = rows;
  g_resize_cols = cols;
  g_resize_calls++;
}

TEST(input_encoder_deccolm_invokes_resize_callback) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(parser);
  ASSERT_NOT_NULL(tb);

  g_resize_calls = 0;
  ansi_parser_set_resize_callback(parser, test_resize_cb, nullptr);

  ansi_parser_feed(parser, "\x1b[?3h", 5, tb);
  ASSERT_EQ(g_resize_calls, 1);
  ASSERT_EQ(g_resize_cols, 132);
  ASSERT_EQ(g_resize_rows, 24);
  ASSERT_EQ(terminal_buffer_get_cols(tb), 132);

  ansi_parser_feed(parser, "\x1b[?3l", 5, tb);
  ASSERT_EQ(g_resize_calls, 2);
  ASSERT_EQ(g_resize_cols, 80);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
}

TEST(input_encoder_pty_smoke_write_encoded_key) {
  PtyManager *pty = pty_manager_init(24, 80, "/bin/sh", nullptr);
  if (!pty) {
    /* Environments without PTY support skip */
    ASSERT_TRUE(true);
    return;
  }

  InputEncoderModes modes = {0};
  char buf[16];
  size_t n =
      input_encoder_encode_key(&modes, INPUT_KEY_RETURN, buf, sizeof(buf));
  ASSERT_EQ(n, 1);
  ASSERT_TRUE(pty_manager_write(pty, buf, n) >= 0);

  pty_manager_destroy(pty);
}

void run_input_encoder_tests(void) {
  printf("\n=== Input Encoder Tests ===\n");
  RUN_TEST(input_encoder_arrow_keys_normal_mode);
  RUN_TEST(input_encoder_arrow_keys_application_mode);
  RUN_TEST(input_encoder_keypad_application_mode);
  RUN_TEST(input_encoder_f1_ss3);
  RUN_TEST(input_encoder_f5_csi);
  RUN_TEST(input_encoder_f12_csi);
  RUN_TEST(input_encoder_shift_up);
  RUN_TEST(input_encoder_ctrl_left);
  RUN_TEST(input_encoder_alt_delete);
  RUN_TEST(input_encoder_shift_f5);
  RUN_TEST(input_encoder_shift_f1);
  RUN_TEST(input_encoder_zero_mods_f1_and_up_unchanged);
  RUN_TEST(input_encoder_focus_in_out);
  RUN_TEST(input_encoder_ctrl_letter);
  RUN_TEST(input_encoder_alt_char);
  RUN_TEST(input_encoder_bracketed_paste);
  RUN_TEST(input_encoder_mouse_sgr);
  RUN_TEST(input_encoder_mouse_x10);
  RUN_TEST(input_encoder_mouse_disabled);
  RUN_TEST(input_encoder_mouse_sgr_wheel_up);
  RUN_TEST(input_encoder_mouse_sgr_wheel_down);
  RUN_TEST(input_encoder_mouse_x10_wheel_up);
  RUN_TEST(input_encoder_mouse_wheel_disabled);
  RUN_TEST(input_encoder_mouse_sgr_modifiers_and_motion_buttons);
  RUN_TEST(input_encoder_mouse_legacy_release_keeps_modifiers);
  RUN_TEST(input_encoder_mouse_utf8_coordinates);
  RUN_TEST(input_encoder_mouse_motion_routing);
  RUN_TEST(input_encoder_mouse_motion_prefers_left_then_middle);
  RUN_TEST(input_encoder_mouse_motion_state_persists_across_updates);
  RUN_TEST(input_encoder_mouse_wheel_accumulates_precise_axes);
  RUN_TEST(input_encoder_mouse_wheel_flips_both_axes_and_caps_bursts);
  RUN_TEST(input_encoder_mouse_wheel_carry_does_not_cross_routes);
  RUN_TEST(input_encoder_mouse_wheel_suppresses_cross_axis_noise);
  RUN_TEST(input_encoder_mouse_wheel_equal_axes_prefer_vertical);
  RUN_TEST(input_encoder_mouse_wheel_dominant_axis_clears_suppressed_carry);
  RUN_TEST(input_encoder_mouse_wheel_direction_change_drops_old_carry);
  RUN_TEST(input_encoder_mouse_wheel_maps_vertical_and_horizontal_buttons);
  RUN_TEST(input_encoder_mouse_wheel_route_respects_alternate_scroll_mode);
  RUN_TEST(input_encoder_mouse_press_state_records_only_delivered_reports);
  RUN_TEST(input_encoder_mouse_press_state_restores_protocol_snapshot);
  RUN_TEST(input_encoder_mouse_press_state_tracks_buttons_and_contexts);
  RUN_TEST(input_encoder_mouse_press_filter_rejects_undelivered_buttons);
  RUN_TEST(input_encoder_mouse_press_filter_keeps_only_outstanding_buttons);
  RUN_TEST(
      input_encoder_mouse_press_filter_rejects_button_taken_on_focus_loss);
  RUN_TEST(input_encoder_mouse_press_filter_composes_with_motion_modes);
  RUN_TEST(input_encoder_local_mouse_reporting_off);
  RUN_TEST(input_encoder_local_mouse_reporting_without_shift);
  RUN_TEST(input_encoder_local_mouse_shift_overrides_1000);
  RUN_TEST(input_encoder_local_mouse_shift_overrides_1002);
  RUN_TEST(input_encoder_mouse_up_to_pty_accepts_delivered_press);
  RUN_TEST(input_encoder_mouse_up_to_pty_local_without_press);
  RUN_TEST(input_encoder_mouse_up_to_pty_rejects_failed_press);
  RUN_TEST(
      input_encoder_mouse_up_to_pty_rejects_press_consumed_on_focus_loss);
  RUN_TEST(input_encoder_modes_from_parser);
  RUN_TEST(input_encoder_modes_from_parser_defaults);
  RUN_TEST(input_encoder_deccolm_invokes_resize_callback);
  RUN_TEST_TIMEOUT(input_encoder_pty_smoke_write_encoded_key, 5);
}
