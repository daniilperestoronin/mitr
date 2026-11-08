/**
 * app_input.h - Host input: keys, paste, mouse (SDL in the app layer)
 *
 * Encodes PTY bytes through input_encoder (no SDL there). Search KEYDOWN is
 * routed to app_search; other KEYDOWN mapping lives here.
 */

#ifndef MITR_APP_INPUT_H
#define MITR_APP_INPUT_H

#include "ansi_parser.h"
#include "app_search.h"
#include "common.h"
#include "config.h"
#include "font_manager.h"
#include "input_encoder.h"
#include "mouse.h"
#include "pty_manager.h"
#include "renderer.h"
#include "terminal_buffer.h"
#include <SDL2/SDL.h>

typedef struct {
  TerminalBuffer *term_buffer;
  AnsiParser *ansi_parser;
  PtyManager *pty_manager;
  MouseManager *mouse_manager;
  FontManager *font_manager;
  Renderer *renderer;
  const MitrConfig *config;
  AppSearch *search;
  bool *running;
  bool *needs_render;
  bool *swallow_next_textinput;
  int *pending_resize_w;
  int *pending_resize_h;
  bool *has_pending_resize;
  InputMouseMotionState *mouse_motion;
  InputMousePressState *mouse_press;
  InputMouseWheelState *mouse_wheel;
} AppInputCtx;

InputKeyMods app_mods_from_sdl(SDL_Keymod mod);

bool app_input_pty_write(PtyManager *pty, const char *data, size_t len);

void app_input_handle_events(AppInputCtx *ctx);

#endif /* MITR_APP_INPUT_H */
