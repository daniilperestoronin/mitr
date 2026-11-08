/**
 * app_input.c - SDL key, paste, and mouse handling for the app layer
 */

#include "app_input.h"

static void app_input_mark_render(AppInputCtx *ctx) {
  if (ctx && ctx->needs_render) {
    *ctx->needs_render = true;
  }
}

static void app_input_set_swallow(AppInputCtx *ctx, bool swallow) {
  if (ctx && ctx->swallow_next_textinput) {
    *ctx->swallow_next_textinput = swallow;
  }
}

bool app_input_pty_write(PtyManager *pty, const char *data, size_t len) {
  if (!pty || !data || len == 0) {
    return false;
  }

  ssize_t n = pty_manager_write_all(pty, data, len);
  if (n < 0) {
    log_warn("PTY write failed");
    return false;
  }
  if ((size_t)n < len) {
    log_warn("PTY write truncated: %zu of %zu bytes", (size_t)n, len);
    return false;
  }
  return true;
}

static bool app_input_pty_write_ctx(AppInputCtx *ctx, const char *data,
                                    size_t len) {
  if (!ctx) {
    return false;
  }
  return app_input_pty_write(ctx->pty_manager, data, len);
}

static void app_paste_text(AppInputCtx *ctx, const char *text) {
  InputEncoderModes modes;
  char wrap[16];
  size_t n;

  if (!ctx || !text) {
    return;
  }

  input_encoder_modes_from_parser(&modes, ctx->ansi_parser);

  n = input_encoder_paste_prefix(&modes, wrap, sizeof(wrap));
  if (n > 0) {
    app_input_pty_write_ctx(ctx, wrap, n);
  }

  app_input_pty_write_ctx(ctx, text, strlen(text));

  n = input_encoder_paste_suffix(&modes, wrap, sizeof(wrap));
  if (n > 0) {
    app_input_pty_write_ctx(ctx, wrap, n);
  }
}

InputKeyMods app_mods_from_sdl(SDL_Keymod mod) {
  InputKeyMods mods = {0};
  mods.shift = (mod & KMOD_SHIFT) != 0;
  mods.alt = (mod & (KMOD_LALT | KMOD_RALT)) != 0;
  mods.ctrl = (mod & KMOD_CTRL) != 0;
  return mods;
}

static void app_send_key(AppInputCtx *ctx, InputKey key, InputKeyMods mods) {
  InputEncoderModes modes;
  char buf[16];
  size_t n;

  if (!ctx) {
    return;
  }
  input_encoder_modes_from_parser(&modes, ctx->ansi_parser);
  n = input_encoder_encode_key_ex(&modes, key, mods, buf, sizeof(buf));
  if (n > 0) {
    app_input_pty_write_ctx(ctx, buf, n);
  }
}

static void app_send_function_key(AppInputCtx *ctx, SDL_Keycode key,
                                  InputKeyMods mods) {
  app_send_key(ctx, (InputKey)(INPUT_KEY_F1 + (key - SDLK_F1)), mods);
}

static bool app_is_search_chord(SDL_Keycode key, SDL_Keymod mod) {
  if (key != SDLK_f || (mod & KMOD_SHIFT) == 0) {
    return false;
  }
  return (mod & (KMOD_CTRL | KMOD_GUI)) != 0;
}

static bool app_handle_search_key(AppInputCtx *ctx, SDL_Keycode key,
                                  SDL_Keymod mod) {
  if (!ctx || !ctx->search) {
    return true;
  }
  if (key == SDLK_ESCAPE) {
    app_search_exit(ctx->search, ctx->renderer);
    return true;
  }
  if (key == SDLK_BACKSPACE) {
    app_input_set_swallow(ctx, true);
    app_search_backspace(ctx->search, ctx->renderer);
    return true;
  }
  if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
    app_input_set_swallow(ctx, true);
    app_search_find(ctx->search, ctx->term_buffer, ctx->renderer,
                    (mod & KMOD_SHIFT) != 0);
    return true;
  }
  if (key == SDLK_g && (mod & KMOD_CTRL) != 0) {
    app_input_set_swallow(ctx, true);
    app_search_find(ctx->search, ctx->term_buffer, ctx->renderer, false);
    return true;
  }
  /* Other keys that would go to the shell are ignored. */
  return true;
}

static void app_report_focus(AppInputCtx *ctx, bool gained) {
  AnsiParserModes modes;
  char buf[8];
  size_t n;

  if (!ctx || !ctx->ansi_parser) {
    return;
  }
  ansi_parser_get_modes(ctx->ansi_parser, &modes);
  if (!modes.focus_event_mode) {
    return;
  }
  n = gained ? input_encoder_encode_focus_in(buf, sizeof(buf))
             : input_encoder_encode_focus_out(buf, sizeof(buf));
  app_input_pty_write_ctx(ctx, buf, n);
}

/* Map a window pixel to the text grid using strict or gesture-clamp policy. */
static bool app_pixel_to_cell(const AppInputCtx *ctx, int pixel_x, int pixel_y,
                              MouseCellPolicy policy, size_t *row,
                              size_t *col) {
  int char_width = 0;
  int char_height = 0;
  size_t cols;
  size_t rows;

  if (!ctx || !ctx->renderer || !ctx->term_buffer || !ctx->config || !row ||
      !col) {
    return false;
  }

  renderer_get_logical_cell_size(ctx->renderer, &char_width, &char_height);
  if (char_width <= 0 || char_height <= 0) {
    return false;
  }

  cols = terminal_buffer_get_cols(ctx->term_buffer);
  rows = terminal_buffer_get_rows(ctx->term_buffer);
  return mouse_pixel_to_cell(pixel_x, pixel_y, ctx->config->padding_x,
                             ctx->config->padding_y, char_width, char_height,
                             rows, cols, policy, row, col);
}

static void app_copy_selection(AppInputCtx *ctx, bool log_shortcut) {
  char *selected_text;

  if (!ctx || !ctx->mouse_manager || !ctx->term_buffer) {
    return;
  }
  selected_text =
      mouse_manager_get_selected_text(ctx->mouse_manager, ctx->term_buffer);
  if (selected_text && selected_text[0] != '\0') {
    SDL_SetClipboardText(selected_text);
    if (log_shortcut) {
      log_debug("Copied to clipboard (Cmd+C/Ctrl+C): %zu bytes",
                strlen(selected_text));
    } else {
      log_debug("Copied to clipboard: %zu bytes", strlen(selected_text));
    }
  }
  free(selected_text);
}

static void app_paste_clipboard(AppInputCtx *ctx, const char *log_label) {
  char *text;

  if (!SDL_HasClipboardText()) {
    return;
  }
  text = SDL_GetClipboardText();
  if (!text) {
    return;
  }
  app_paste_text(ctx, text);
  log_debug("Pasted from clipboard (%s): %zu bytes", log_label, strlen(text));
  SDL_free(text);
}

static void app_handle_keydown(AppInputCtx *ctx, SDL_Keycode key,
                               SDL_Keymod mod) {
  InputKeyMods mods = app_mods_from_sdl(mod);
  bool search_active = ctx->search && ctx->search->active;

  if (key == SDLK_q && (mod & KMOD_CTRL)) {
    log_debug("Ctrl+Q pressed, quitting");
    if (ctx->running) {
      *ctx->running = false;
    }
    app_input_mark_render(ctx);
  } else if (app_is_search_chord(key, mod)) {
    app_input_set_swallow(ctx, true);
    app_search_enter(ctx->search, ctx->renderer);
  } else if (search_active) {
    app_handle_search_key(ctx, key, mod);
  } else if (key == SDLK_c && ((mod & KMOD_GUI) || (mod & KMOD_CTRL))) {
    if (mouse_manager_is_selecting(ctx->mouse_manager)) {
      app_copy_selection(ctx, true);
    } else if (mod & KMOD_CTRL) {
      char buf[4];
      size_t n = input_encoder_encode_ctrl('c', buf, sizeof(buf));
      app_input_pty_write_ctx(ctx, buf, n);
      log_debug("Sent Ctrl+C to shell (SIGINT)");
    }
  } else if (key == SDLK_v && ((mod & KMOD_GUI) || (mod & KMOD_CTRL))) {
    app_paste_clipboard(ctx, "Cmd+V/Ctrl+V");
  } else if (key == SDLK_INSERT && (mod & KMOD_SHIFT)) {
    app_paste_clipboard(ctx, "Shift+Insert");
  } else if ((mod & KMOD_CTRL) && key >= SDLK_a && key <= SDLK_z &&
             key != SDLK_c && key != SDLK_v) {
    char buf[4];
    size_t n = input_encoder_encode_ctrl((char)('a' + (key - SDLK_a)), buf,
                                         sizeof(buf));
    app_input_pty_write_ctx(ctx, buf, n);
  } else if (mods.alt && !mods.ctrl && key >= SDLK_a && key <= SDLK_z) {
    char buf[4];
    size_t n = input_encoder_encode_alt_char((char)('a' + (key - SDLK_a)), buf,
                                             sizeof(buf));
    if (n > 0) {
      app_input_pty_write_ctx(ctx, buf, n);
      app_input_set_swallow(ctx, true);
    }
  } else if (key == SDLK_ESCAPE) {
    app_send_key(ctx, INPUT_KEY_ESCAPE, mods);
  } else if (key == SDLK_UP) {
    app_send_key(ctx, INPUT_KEY_UP, mods);
  } else if (key == SDLK_DOWN) {
    app_send_key(ctx, INPUT_KEY_DOWN, mods);
  } else if (key == SDLK_RIGHT) {
    app_send_key(ctx, INPUT_KEY_RIGHT, mods);
  } else if (key == SDLK_LEFT) {
    app_send_key(ctx, INPUT_KEY_LEFT, mods);
  } else if (key == SDLK_HOME) {
    app_send_key(ctx, INPUT_KEY_HOME, mods);
  } else if (key == SDLK_END) {
    app_send_key(ctx, INPUT_KEY_END, mods);
  } else if (key == SDLK_RETURN) {
    app_send_key(ctx, INPUT_KEY_RETURN, mods);
  } else if (key == SDLK_KP_ENTER) {
    app_send_key(ctx, INPUT_KEY_KP_ENTER, mods);
  } else if (key == SDLK_BACKSPACE) {
    app_send_key(ctx, INPUT_KEY_BACKSPACE, mods);
  } else if (key == SDLK_TAB) {
    app_send_key(ctx, INPUT_KEY_TAB, mods);
  } else if (key == SDLK_INSERT) {
    app_send_key(ctx, INPUT_KEY_INSERT, mods);
  } else if (key == SDLK_DELETE) {
    app_send_key(ctx, INPUT_KEY_DELETE, mods);
  } else if ((mod & KMOD_SHIFT) &&
             (key == SDLK_PAGEUP || key == SDLK_PAGEDOWN) &&
             !terminal_buffer_is_alt_screen(ctx->term_buffer)) {
    size_t rows = terminal_buffer_get_rows(ctx->term_buffer);
    if (rows > 0) {
      int delta = (int)(rows - 1);
      if (key == SDLK_PAGEDOWN) {
        delta = -delta;
      }
      terminal_buffer_scroll_view(ctx->term_buffer, delta);
    }
  } else if (key == SDLK_PAGEUP) {
    app_send_key(ctx, INPUT_KEY_PAGEUP, mods);
  } else if (key == SDLK_PAGEDOWN) {
    app_send_key(ctx, INPUT_KEY_PAGEDOWN, mods);
  } else if (key >= SDLK_F1 && key <= SDLK_F12) {
    app_send_function_key(ctx, key, mods);
  }
  /* Printable characters are handled by SDL_TEXTINPUT */

  app_input_mark_render(ctx);
}

static int app_mouse_button_code(Uint8 button) {
  if (button == SDL_BUTTON_MIDDLE) {
    return 1;
  }
  if (button == SDL_BUTTON_RIGHT) {
    return 2;
  }
  return 0;
}

static bool app_mouse_shift_held(void) {
  return (SDL_GetModState() & KMOD_SHIFT) != 0;
}

static bool app_mouse_update_cell(AppInputCtx *ctx, size_t row, size_t col) {
  return ctx && input_encoder_mouse_motion_cell_changed(ctx->mouse_motion, row,
                                                        col);
}

static InputMouseButtons app_mouse_buttons(Uint32 state) {
  return (InputMouseButtons){
      .left = (state & SDL_BUTTON_LMASK) != 0,
      .middle = (state & SDL_BUTTON_MMASK) != 0,
      .right = (state & SDL_BUTTON_RMASK) != 0,
  };
}

/* Host selection when reporting is off or Shift is held. A local drag stays
 * local if Shift is released before the button. */
static bool app_mouse_use_local(AppInputCtx *ctx, int mouse_report_mode) {
  return input_encoder_local_mouse(mouse_report_mode, app_mouse_shift_held()) ||
         mouse_manager_is_dragging(ctx->mouse_manager);
}

static bool app_send_mouse_report(AppInputCtx *ctx,
                                  const InputEncoderModes *modes,
                                  InputMouseAction action, int button,
                                  size_t row, size_t col) {
  char buf[64];
  InputKeyMods mods = app_mods_from_sdl(SDL_GetModState());
  size_t n = input_encoder_encode_mouse(modes, action, button, mods, row, col,
                                        buf, sizeof(buf));
  return n > 0 && app_input_pty_write_ctx(ctx, buf, n);
}

static void app_mouse_update_capture(AppInputCtx *ctx) {
  SDL_bool capture = input_encoder_mouse_press_pending(ctx->mouse_press)
                         ? SDL_TRUE
                         : SDL_FALSE;
  if (SDL_CaptureMouse(capture) != 0) {
    log_warn("Failed to %s mouse capture: %s",
             capture == SDL_TRUE ? "enable" : "disable", SDL_GetError());
  }
}

static void app_handle_mouse_button_down(AppInputCtx *ctx,
                                         const SDL_MouseButtonEvent *button) {
  size_t row = 0;
  size_t col = 0;
  InputEncoderModes modes;

  if (button->button != SDL_BUTTON_LEFT &&
      button->button != SDL_BUTTON_MIDDLE &&
      button->button != SDL_BUTTON_RIGHT) {
    return;
  }
  if (!app_pixel_to_cell(ctx, button->x, button->y, MOUSE_CELL_STRICT, &row,
                         &col)) {
    return;
  }
  app_mouse_update_cell(ctx, row, col);

  input_encoder_modes_from_parser(&modes, ctx->ansi_parser);
  /* A finished host highlight must not steal the next click from tmux. */
  if (!input_encoder_local_mouse(modes.mouse_report_mode,
                                 app_mouse_shift_held())) {
    mouse_manager_clear_selection(ctx->mouse_manager);
  }
  if (!app_mouse_use_local(ctx, modes.mouse_report_mode)) {
    int code = app_mouse_button_code(button->button);
    bool delivered =
        app_send_mouse_report(ctx, &modes, INPUT_MOUSE_PRESS, code, row, col);
    input_encoder_mouse_press_record(ctx->mouse_press, code, &modes, delivered);
    if (delivered) {
      app_mouse_update_capture(ctx);
    }
  } else if (button->button == SDL_BUTTON_LEFT) {
    SDL_Keymod mod = SDL_GetModState();
    if ((mod & (KMOD_CTRL | KMOD_GUI)) != 0) {
      const char *uri =
          terminal_buffer_get_hyperlink_at(ctx->term_buffer, row, col);
      if (uri && (strncmp(uri, "http://", 7) == 0 ||
                  strncmp(uri, "https://", 8) == 0)) {
        if (SDL_OpenURL(uri) != 0) {
          log_warn("Failed to open hyperlink: %s", SDL_GetError());
        }
      }
    } else if (button->clicks >= 3) {
      mouse_manager_select_line(ctx->mouse_manager, ctx->term_buffer, row);
      log_debug("Line selection at row %zu", row);
    } else if (button->clicks == 2) {
      mouse_manager_select_word(ctx->mouse_manager, ctx->term_buffer, row, col);
      log_debug("Word selection at %zu:%zu", row, col);
    } else {
      mouse_manager_start_selection(ctx->mouse_manager, row, col);
      log_debug("Selection started at %zu:%zu", row, col);
    }
  }
  app_input_mark_render(ctx);
}

static void app_handle_mouse_motion(AppInputCtx *ctx,
                                    const SDL_MouseMotionEvent *motion) {
  size_t row = 0;
  size_t col = 0;
  InputEncoderModes modes;
  InputMouseButtons buttons;
  bool cell_changed;
  bool active_gesture;
  MouseCellPolicy policy;
  int button;

  buttons = app_mouse_buttons(motion->state);
  input_encoder_modes_from_parser(&modes, ctx->ansi_parser);
  active_gesture = mouse_manager_is_dragging(ctx->mouse_manager) ||
                   input_encoder_mouse_press_pending(ctx->mouse_press);
  policy = active_gesture ? MOUSE_CELL_CLAMP : MOUSE_CELL_STRICT;
  if (!app_pixel_to_cell(ctx, motion->x, motion->y, policy, &row, &col)) {
    if (ctx->mouse_motion) {
      ctx->mouse_motion->cell_valid = false;
    }
    return;
  }

  cell_changed = app_mouse_update_cell(ctx, row, col);
  if (app_mouse_use_local(ctx, modes.mouse_report_mode)) {
    if (buttons.left) {
      mouse_manager_update_selection(ctx->mouse_manager, row, col);
      app_input_mark_render(ctx);
    }
    return;
  }
  buttons =
      input_encoder_mouse_press_filter_buttons(ctx->mouse_press, buttons);
  if (input_encoder_mouse_motion_report(modes.mouse_report_mode, cell_changed,
                                        buttons, &button)) {
    app_send_mouse_report(ctx, &modes, INPUT_MOUSE_MOVE, button, row, col);
  }
}

static void app_handle_mouse_button_up(AppInputCtx *ctx,
                                       const SDL_MouseButtonEvent *button) {
  size_t row = 0;
  size_t col = 0;
  InputEncoderModes modes;
  InputEncoderModes press_modes;
  int code;
  bool outstanding;
  bool use_local;
  bool local_drag;
  bool mapped;

  if (button->button != SDL_BUTTON_LEFT &&
      button->button != SDL_BUTTON_MIDDLE &&
      button->button != SDL_BUTTON_RIGHT) {
    return;
  }

  code = app_mouse_button_code(button->button);
  outstanding =
      input_encoder_mouse_press_take(ctx->mouse_press, code, &press_modes);
  input_encoder_modes_from_parser(&modes, ctx->ansi_parser);
  use_local = app_mouse_use_local(ctx, modes.mouse_report_mode);
  local_drag = mouse_manager_is_dragging(ctx->mouse_manager);
  mapped = app_pixel_to_cell(ctx, button->x, button->y,
                             outstanding || local_drag ? MOUSE_CELL_CLAMP
                                                       : MOUSE_CELL_STRICT,
                             &row, &col);
  if (!mapped) {
    if (!outstanding) {
      return;
    }
    row = 0;
    col = 0;
  }

  if (input_encoder_mouse_up_to_pty(outstanding)) {
    app_send_mouse_report(ctx, &press_modes, INPUT_MOUSE_RELEASE, code, row,
                          col);
  } else if (use_local && button->button == SDL_BUTTON_LEFT) {
    mouse_manager_finish_selection(ctx->mouse_manager);
    app_copy_selection(ctx, false);
  } else if (use_local && button->button == SDL_BUTTON_MIDDLE) {
    app_paste_clipboard(ctx, "middle button");
  }
  app_mouse_update_capture(ctx);
  app_input_mark_render(ctx);
}

static void app_release_reported_mouse_buttons(AppInputCtx *ctx) {
  size_t row = 0;
  size_t col = 0;

  if (!ctx || !ctx->mouse_press) {
    return;
  }
  if (ctx->mouse_motion && ctx->mouse_motion->cell_valid) {
    row = ctx->mouse_motion->row;
    col = ctx->mouse_motion->col;
  }
  for (int button = 0; button <= 2; button++) {
    InputEncoderModes modes;
    if (input_encoder_mouse_press_take(ctx->mouse_press, button, &modes)) {
      app_send_mouse_report(ctx, &modes, INPUT_MOUSE_RELEASE, button, row, col);
    }
  }
  app_mouse_update_capture(ctx);
}

static double app_wheel_delta(int coarse, float precise) {
  return precise != 0.0F ? (double)precise : (double)coarse;
}

static void app_send_wheel_reports(AppInputCtx *ctx,
                                   const InputEncoderModes *modes,
                                   bool horizontal, int steps, size_t row,
                                   size_t col) {
  int count = steps < 0 ? -steps : steps;
  int button = input_encoder_mouse_wheel_button(horizontal, steps);

  for (int i = 0; i < count; i++) {
    app_send_mouse_report(ctx, modes, INPUT_MOUSE_PRESS, button, row, col);
  }
}

static void app_send_wheel_keys(AppInputCtx *ctx, InputMouseWheelSteps steps) {
  InputKey vertical = steps.y > 0 ? INPUT_KEY_UP : INPUT_KEY_DOWN;
  InputKey horizontal = steps.x > 0 ? INPUT_KEY_RIGHT : INPUT_KEY_LEFT;
  int count_y = steps.y < 0 ? -steps.y : steps.y;
  int count_x = steps.x < 0 ? -steps.x : steps.x;

  for (int i = 0; i < count_y; i++) {
    app_send_key(ctx, vertical, (InputKeyMods){0});
  }
  for (int i = 0; i < count_x; i++) {
    app_send_key(ctx, horizontal, (InputKeyMods){0});
  }
}

static void app_handle_mouse_wheel(AppInputCtx *ctx,
                                   const SDL_MouseWheelEvent *wheel) {
  InputMouseWheelSteps steps;
  InputMouseWheelRoute route;
  bool search_active = ctx->search && ctx->search->active;
  InputEncoderModes modes;
  bool alt_screen;
  size_t row = 0;
  size_t col = 0;

  if (!app_pixel_to_cell(ctx, wheel->mouseX, wheel->mouseY, MOUSE_CELL_STRICT,
                         &row, &col)) {
    return;
  }

  alt_screen = terminal_buffer_is_alt_screen(ctx->term_buffer);
  input_encoder_modes_from_parser(&modes, ctx->ansi_parser);
  route = input_encoder_mouse_wheel_route(
      search_active, app_mouse_shift_held(), modes.mouse_report_mode,
      alt_screen, modes.alternate_scroll);
  steps = input_encoder_mouse_wheel_accumulate(
      ctx->mouse_wheel, route,
      app_wheel_delta(wheel->x, wheel->preciseX),
      app_wheel_delta(wheel->y, wheel->preciseY),
      wheel->direction == SDL_MOUSEWHEEL_FLIPPED);
  if (steps.x == 0 && steps.y == 0) {
    return;
  }

  if (route == INPUT_MOUSE_WHEEL_LOCAL) {
    /* Overlay or Shift: do not send keys or mouse reports (64..67). */
    if (!alt_screen) {
      terminal_buffer_scroll_view(ctx->term_buffer, steps.y * 3);
    }
  } else if (route == INPUT_MOUSE_WHEEL_REPORT) {
    app_send_wheel_reports(ctx, &modes, false, steps.y, row, col);
    app_send_wheel_reports(ctx, &modes, true, steps.x, row, col);
  } else if (route == INPUT_MOUSE_WHEEL_ALTERNATE) {
    app_send_wheel_keys(ctx, steps);
  }
  app_input_mark_render(ctx);
}

static void app_handle_textinput(AppInputCtx *ctx, const char *text) {
  bool swallow = ctx->swallow_next_textinput && *ctx->swallow_next_textinput;
  bool search_active = ctx->search && ctx->search->active;

  app_input_set_swallow(ctx, false);
  if (swallow) {
    return;
  }
  if (search_active) {
    app_search_append(ctx->search, ctx->renderer, text);
    app_input_mark_render(ctx);
    return;
  }
  app_input_pty_write_ctx(ctx, text, strlen(text));
  app_input_mark_render(ctx);
}

static void app_handle_window_event(AppInputCtx *ctx,
                                    const SDL_WindowEvent *window) {
  if (window->event == SDL_WINDOWEVENT_RESIZED) {
    if (ctx->pending_resize_w) {
      *ctx->pending_resize_w = window->data1;
    }
    if (ctx->pending_resize_h) {
      *ctx->pending_resize_h = window->data2;
    }
    if (ctx->has_pending_resize) {
      *ctx->has_pending_resize = true;
    }
  } else if (window->event == SDL_WINDOWEVENT_EXPOSED) {
    terminal_buffer_mark_all_dirty(ctx->term_buffer);
    renderer_force_full_redraw(ctx->renderer);
    app_input_mark_render(ctx);
  } else if (window->event == SDL_WINDOWEVENT_FOCUS_GAINED) {
    app_report_focus(ctx, true);
  } else if (window->event == SDL_WINDOWEVENT_FOCUS_LOST) {
    app_release_reported_mouse_buttons(ctx);
    mouse_manager_focus_lost(ctx->mouse_manager);
    app_report_focus(ctx, false);
    app_input_mark_render(ctx);
  }
}

void app_input_handle_events(AppInputCtx *ctx) {
  SDL_Event event;

  if (!ctx) {
    return;
  }

  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT:
      log_debug("SDL_QUIT event received");
      if (ctx->running) {
        *ctx->running = false;
      }
      app_input_mark_render(ctx);
      break;

    case SDL_KEYDOWN:
      app_handle_keydown(ctx, event.key.keysym.sym, event.key.keysym.mod);
      break;

    case SDL_TEXTINPUT:
      app_handle_textinput(ctx, event.text.text);
      break;

    case SDL_WINDOWEVENT:
      app_handle_window_event(ctx, &event.window);
      break;

    case SDL_MOUSEBUTTONDOWN:
      app_handle_mouse_button_down(ctx, &event.button);
      break;

    case SDL_MOUSEMOTION:
      app_handle_mouse_motion(ctx, &event.motion);
      break;

    case SDL_MOUSEBUTTONUP:
      app_handle_mouse_button_up(ctx, &event.button);
      break;

    case SDL_MOUSEWHEEL:
      app_handle_mouse_wheel(ctx, &event.wheel);
      break;

    default:
      break;
    }
  }
}
