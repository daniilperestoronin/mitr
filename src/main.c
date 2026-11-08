/**
 * main.c - Main entry point for mitr
 */

#include "common.h"
#include "config.h"
#include "font_manager.h"
#include "renderer.h"
#include "terminal_buffer.h"

#include "ansi_parser.h"
#include "app_input.h"
#include "app_search.h"
#include "mouse.h"
#include "pty_manager.h"
#include <SDL2/SDL.h>

/* ========================================================================
 * Application State
 * ======================================================================== */

typedef struct {
  MitrConfig *config;
  SDL_Window *window;
  SDL_Renderer *sdl_renderer;
  FontManager *font_manager;
  TerminalBuffer *term_buffer;
  Renderer *renderer;

  AnsiParser *ansi_parser;
  PtyManager *pty_manager;
  MouseManager *mouse_manager;
  bool running;
  bool needs_render;

  /* Deferred resize: only the last resize per frame is applied */
  int pending_resize_w;
  int pending_resize_h;
  bool has_pending_resize;

  bool sdl_initialized;

  /* Skip the SDL_TEXTINPUT that follows an Alt+letter KEYDOWN (ESC+letter). */
  bool swallow_next_textinput;

  InputMouseMotionState mouse_motion;
  InputMousePressState mouse_press;
  InputMouseWheelState mouse_wheel;

  AppSearch search;

  bool sync_output_was_on;
  uint32_t sync_output_started_ms;
  uint32_t sync_output_generation;
  bool sync_held_frame;
} AppState;

static void app_cleanup(AppState *app);

static const char *mitr_error_string(MitrError err) {
  switch (err) {
  case MITR_OK:
    return "ok";
  case MITR_ERROR_MEMORY:
    return "memory allocation failed";
  case MITR_ERROR_SDL:
    return "SDL/graphics failure";
  case MITR_ERROR_PTY:
    return "PTY/shell failure";
  case MITR_ERROR_INVALID_PARAM:
    return "invalid parameter";
  case MITR_ERROR_IO:
    return "I/O failure";
  case MITR_ERROR_CONFIG:
    return "configuration failure";
  case MITR_ERROR_NOT_IMPLEMENTED:
    return "not implemented";
  default:
    return "unknown error";
  }
}

static MitrError app_fail(AppState *app, MitrError err, const char *context) {
  const char *msg = context ? context : mitr_error_string(err);
  log_error("%s (%s)", msg, mitr_error_string(err));
  fprintf(stderr, "ERROR: %s\n", msg);
  if (app)
    app_cleanup(app);
  return err;
}

static void app_on_parser_resize(void *userdata, size_t rows, size_t cols) {
  AppState *app = (AppState *)userdata;
  if (!app || !app->pty_manager)
    return;
  pty_manager_resize(app->pty_manager, rows, cols);
  app->needs_render = true;
  log_debug("PTY resized from parser callback: %zux%zu", rows, cols);
}

static void app_on_parser_title(void *userdata, const char *title) {
  AppState *app = (AppState *)userdata;
  if (!app || !app->window || !title)
    return;
  SDL_SetWindowTitle(app->window, title);
}

static void app_on_parser_reply(void *userdata, const char *data, size_t len) {
  AppState *app = (AppState *)userdata;
  if (!app) {
    return;
  }
  app_input_pty_write(app->pty_manager, data, len);
}

static bool app_osc52_user_accepted(AppState *app, const char *text,
                                    size_t len) {
  char message[256];
  const SDL_MessageBoxButtonData buttons[] = {
      {.flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,
       .buttonid = 0,
       .text = "No"},
      {.flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
       .buttonid = 1,
       .text = "Yes"},
  };
  SDL_MessageBoxData box = {
      .flags = SDL_MESSAGEBOX_WARNING,
      .window = app ? app->window : nullptr,
      .title = "mitr - clipboard",
      .message = message,
      .numbuttons = 2,
      .buttons = buttons,
      .colorScheme = nullptr,
  };
  int buttonid = 0;

  osc52_format_confirm_message(text, len, message, sizeof(message));
  if (SDL_ShowMessageBox(&box, &buttonid) != 0) {
    log_warn("OSC 52 confirm dialog failed: %s", SDL_GetError());
    return false;
  }
  return buttonid == 1;
}

static void app_on_parser_clipboard(void *userdata, const char *text,
                                    size_t len) {
  AppState *app = (AppState *)userdata;
  Osc52Mode mode;
  bool accepted;
  char *copy;

  if (!app || !app->config || !text) {
    return;
  }

  mode = app->config->osc52;
  accepted = true;
  if (mode == OSC52_CONFIRM) {
    accepted = app_osc52_user_accepted(app, text, len);
  }
  if (!osc52_should_apply(mode, accepted)) {
    log_debug("OSC 52 clipboard write dropped");
    return;
  }

  copy = checked_malloc(len + 1);
  if (!copy) {
    return;
  }
  memcpy(copy, text, len);
  copy[len] = '\0';
  if (SDL_SetClipboardText(copy) != 0) {
    log_warn("Failed to set clipboard text: %s", SDL_GetError());
  }
  free(copy);
}

static bool app_allow_present(AppState *app, bool arm_timer) {
  bool sync_on;
  uint32_t now;
  uint32_t gen;

  if (!app || !app->ansi_parser) {
    return true;
  }
  sync_on = ansi_parser_is_synchronized(app->ansi_parser);
  now = SDL_GetTicks();
  /* Arm only after parse so a pre-feed poll cannot consume a 2026 h edge. */
  if (arm_timer) {
    gen = ansi_parser_sync_generation(app->ansi_parser);
    app->sync_output_started_ms = ansi_parser_sync_started_ms(
        gen, app->sync_output_generation, now, app->sync_output_started_ms);
    if (sync_on && !app->sync_output_was_on) {
      app->sync_output_started_ms = now;
    }
    app->sync_output_generation = gen;
    app->sync_output_was_on = sync_on;
  }
  return ansi_parser_sync_should_present(
      sync_on, now, app->sync_output_started_ms, MITR_SYNC_OUTPUT_TIMEOUT_MS);
}

static void app_handle_events(AppState *app) {
  AppInputCtx ctx = {
      .term_buffer = app->term_buffer,
      .ansi_parser = app->ansi_parser,
      .pty_manager = app->pty_manager,
      .mouse_manager = app->mouse_manager,
      .font_manager = app->font_manager,
      .renderer = app->renderer,
      .config = app->config,
      .search = &app->search,
      .running = &app->running,
      .needs_render = &app->needs_render,
      .swallow_next_textinput = &app->swallow_next_textinput,
      .pending_resize_w = &app->pending_resize_w,
      .pending_resize_h = &app->pending_resize_h,
      .has_pending_resize = &app->has_pending_resize,
      .mouse_motion = &app->mouse_motion,
      .mouse_press = &app->mouse_press,
      .mouse_wheel = &app->mouse_wheel,
  };
  app_input_handle_events(&ctx);
}

/* ========================================================================
 * Initialization
 * ======================================================================== */

/**
 * Initialize application: config already loaded; spawn PTY before SDL so the
 * shell is not blocked behind GPU/font setup. Failures use app_cleanup().
 */
static MitrError app_init(AppState *app) {
  log_trace("Initializing application");

  if (!app || !app->config)
    return app_fail(app, MITR_ERROR_CONFIG, "Config is nullptr");

  /* Provisional grid from window size / font size (refined after font load) */
  int est_ch = app->config->font_size > 0 ? app->config->font_size : 16;
  int est_cw = est_ch > 1 ? (est_ch * 3) / 5 : 8;
  if (est_cw < 1)
    est_cw = 1;
  int usable_w = app->config->window_width - (app->config->padding_x * 2);
  int usable_h = app->config->window_height - (app->config->padding_y * 2);
  if (usable_w < 0)
    usable_w = 0;
  if (usable_h < 0)
    usable_h = 0;
  size_t term_cols = (size_t)(usable_w / est_cw);
  size_t term_rows = (size_t)(usable_h / est_ch);
  if (term_cols < 2)
    term_cols = 2;
  if (term_rows < 2)
    term_rows = 2;
  if (term_cols > MITR_MAX_COLS)
    term_cols = MITR_MAX_COLS;
  if (term_rows > MITR_MAX_ROWS)
    term_rows = MITR_MAX_ROWS;

  /* ---- 1. PTY / shell (before SDL) ---- */
  const char *shell = app->config->shell[0] ? app->config->shell : nullptr;
  app->pty_manager = pty_manager_init(
      term_rows, term_cols, shell,
      app->config->term_env[0] ? app->config->term_env : nullptr);
  if (!app->pty_manager)
    return app_fail(app, MITR_ERROR_PTY, "Failed to initialize PTY manager");

  app->ansi_parser = ansi_parser_init();
  if (!app->ansi_parser)
    return app_fail(app, MITR_ERROR_MEMORY, "Failed to initialize ANSI parser");

  ansi_parser_set_palette(app->ansi_parser, app->config->colors,
                          app->config->bold_is_bright);
  ansi_parser_set_resize_callback(app->ansi_parser, app_on_parser_resize, app);

  app->term_buffer = terminal_buffer_init(term_rows, term_cols);
  if (!app->term_buffer)
    return app_fail(app, MITR_ERROR_MEMORY,
                    "Failed to initialize terminal buffer");

  terminal_buffer_set_colors(app->term_buffer, app->config->foreground,
                             app->config->background);
  if (app->config->scrollback_lines > 0) {
    size_t scrollback = (size_t)app->config->scrollback_lines;
    if (scrollback > MITR_MAX_SCROLLBACK)
      scrollback = MITR_MAX_SCROLLBACK;
    terminal_buffer_set_scrollback_capacity(app->term_buffer, scrollback);
  }
  terminal_buffer_clear(app->term_buffer);

  app->mouse_manager = mouse_manager_init();
  if (!app->mouse_manager)
    return app_fail(app, MITR_ERROR_MEMORY,
                    "Failed to initialize mouse manager");

  /* ---- 2. SDL / window ---- */
  if (SDL_Init(SDL_INIT_VIDEO) < 0)
    return app_fail(app, MITR_ERROR_SDL, SDL_GetError());
  app->sdl_initialized = true;

  /* SDL assumes a game or media player and inhibits idle by default. */
  SDL_EnableScreenSaver();

  Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI;
  if (app->config->window_resizable)
    window_flags |= SDL_WINDOW_RESIZABLE;

  app->window = SDL_CreateWindow(
      WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      app->config->window_width, app->config->window_height, window_flags);
  if (!app->window)
    return app_fail(app, MITR_ERROR_SDL, SDL_GetError());

  ansi_parser_set_title_callback(app->ansi_parser, app_on_parser_title, app);
  ansi_parser_set_reply_callback(app->ansi_parser, app_on_parser_reply, app);
  if (app->config->osc52 != OSC52_OFF) {
    ansi_parser_set_clipboard_callback(app->ansi_parser,
                                       app_on_parser_clipboard, app);
  } else {
    log_debug("OSC 52 clipboard writes disabled (terminal.osc52=off)");
  }

  app->sdl_renderer = SDL_CreateRenderer(
      app->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!app->sdl_renderer)
    return app_fail(app, MITR_ERROR_SDL, SDL_GetError());

  if (app->config->opacity < 1.0f) {
    if (SDL_SetWindowOpacity(app->window, app->config->opacity) != 0)
      log_warn("SDL_SetWindowOpacity failed: %s", SDL_GetError());
  }

  /* ---- 3. Fonts / renderer; refine grid and resize PTY ---- */
  int win_w = 0;
  int win_h = 0;
  int draw_w = 0;
  int draw_h = 0;
  float display_scale;
  int font_px;
  const char *font_path =
      app->config->font_path[0] ? app->config->font_path : nullptr;
  const char *fallback_path = app->config->font_fallback_path[0]
                                  ? app->config->font_fallback_path
                                  : nullptr;

  SDL_GetWindowSize(app->window, &win_w, &win_h);
  if (SDL_GetRendererOutputSize(app->sdl_renderer, &draw_w, &draw_h) != 0 ||
      draw_w <= 0 || draw_h <= 0) {
    draw_w = win_w;
    draw_h = win_h;
  }
  display_scale = mitr_display_scale(win_w, draw_w);
  font_px = mitr_logical_to_drawable(app->config->font_size, display_scale);
  if (font_px < 1) {
    font_px = 1;
  }
  log_debug("HiDPI scale=%.2f window=%dx%d drawable=%dx%d font_px=%d",
            (double)display_scale, win_w, win_h, draw_w, draw_h, font_px);

  app->font_manager =
      font_manager_init(app->config->font_family, font_path, fallback_path,
                        font_px, app->sdl_renderer);
  if (!app->font_manager)
    return app_fail(app, MITR_ERROR_SDL, "Failed to initialize font manager");

  int char_width = 0;
  int char_height = 0;
  font_manager_get_char_size(app->font_manager, &char_width, &char_height);
  if (char_width <= 0 || char_height <= 0)
    return app_fail(app, MITR_ERROR_SDL, "Invalid font metrics");

  app->renderer =
      renderer_init(app->window, app->sdl_renderer, app->font_manager);
  if (!app->renderer)
    return app_fail(app, MITR_ERROR_SDL, "Failed to initialize renderer");

  renderer_set_appearance(app->renderer, app->config->padding_x,
                          app->config->padding_y, app->config->cursor_color,
                          app->config->cursor_style,
                          app->config->selection_color);

  renderer_compute_grid(app->renderer, win_w, win_h, &term_rows, &term_cols);

  if (terminal_buffer_resize(app->term_buffer, term_rows, term_cols) != MITR_OK)
    return app_fail(app, MITR_ERROR_MEMORY, "Failed to resize terminal buffer");
  if (pty_manager_resize(app->pty_manager, term_rows, term_cols) != MITR_OK)
    return app_fail(app, MITR_ERROR_PTY, "Failed to resize PTY");

  app_search_init(&app->search);
  app->running = true;
  app->needs_render = true;

  log_debug("Application initialized successfully");
  log_debug("Terminal size: %zux%zu", term_rows, term_cols);
  return MITR_OK;
}

/* ========================================================================
 * Main Loop
 * ======================================================================== */

/**
 * Apply deferred resize (only the last one per frame).
 */
static void app_apply_pending_resize(AppState *app) {
  if (!app->has_pending_resize) {
    return;
  }
  app->has_pending_resize = false;

  int w = app->pending_resize_w;
  int h = app->pending_resize_h;
  log_debug("Applying resize: %dx%d pixels", w, h);

  renderer_update_size(app->renderer, w, h);
  /* Keep padding/appearance in sync on resize */
  renderer_set_appearance(app->renderer, app->config->padding_x,
                          app->config->padding_y, app->config->cursor_color,
                          app->config->cursor_style,
                          app->config->selection_color);

  if (w < 0 || h < 0) {
    return;
  }

  size_t new_rows = 0;
  size_t new_cols = 0;
  renderer_compute_grid(app->renderer, w, h, &new_rows, &new_cols);

  log_debug("New terminal size: %zux%zu chars", new_rows, new_cols);

  if (terminal_buffer_resize(app->term_buffer, new_rows, new_cols) == MITR_OK) {
    pty_manager_resize(app->pty_manager, new_rows, new_cols);
  }

  renderer_force_full_redraw(app->renderer);
  terminal_buffer_mark_all_dirty(app->term_buffer);
  app->needs_render = true;
}

/**
 * Main application loop
 *
 * Rendering is throttled by VSYNC (SDL_RenderPresent blocks until vblank).
 * When idle (no pending render), we poll the PTY fd for up to 10 ms
 * (POLLIN, and POLLOUT when output is queued), so an idle terminal uses
 * near-zero CPU while still draining the non-blocking write queue.
 */
static void app_run(AppState *app) {
  log_debug("Entering main loop");

  char pty_buffer[4096];

  while (app->running) {
    /* Handle SDL events (resize is deferred, not applied inline) */
    app_handle_events(app);

    /* Apply the single latest resize after all events are consumed */
    app_apply_pending_resize(app);

    /* Drain any queued keyboard/paste bytes without blocking the UI */
    if (pty_manager_flush(app->pty_manager) < 0) {
      log_warn("PTY output flush failed");
    }

    /* Read from PTY.  When a render is already pending we only grab
     * what is immediately available (non-blocking) so we don't stall
     * the frame.  When idle we let pty_manager_read poll for up to
     * 10 ms — this is what keeps CPU usage near zero.  While
     * synchronized output is holding back Present, keep polling so
     * we do not busy-spin. */
    bool allow_present = app_allow_present(app, false);
    bool waiting_sync = app->needs_render && !allow_present;
    ssize_t bytes;
    if (app->needs_render && !waiting_sync) {
      bytes = pty_manager_read_available(app->pty_manager, pty_buffer,
                                         sizeof(pty_buffer) - 1);
    } else {
      bytes = pty_manager_read(app->pty_manager, pty_buffer,
                               sizeof(pty_buffer) - 1);
    }

    if (bytes > 0) {
      pty_buffer[bytes] = '\0';
      ansi_parser_feed(app->ansi_parser, pty_buffer, (size_t)bytes,
                       app->term_buffer);
      app->needs_render = true;

      /* Drain remaining data with a small limit to avoid blocking
       * on large bursts (e.g. Helix scroll). Lower limit = faster
       * first paint, higher = fewer partial redraws. */
      int drain_limit = 16;
      while (drain_limit-- > 0) {
        bytes = pty_manager_read_available(app->pty_manager, pty_buffer,
                                           sizeof(pty_buffer) - 1);
        if (bytes <= 0)
          break;
        pty_buffer[bytes] = '\0';
        ansi_parser_feed(app->ansi_parser, pty_buffer, (size_t)bytes,
                         app->term_buffer);
      }
    }

    /* Check if shell exited */
    if (!pty_manager_is_alive(app->pty_manager)) {
      log_debug("Shell exited");
      app->running = false;
    }

    /* Only render when something changed.
     * SDL_RenderPresent (inside renderer_render) blocks on VSYNC,
     * which naturally limits us to the display refresh rate.
     * DEC 2026 holds Present until CSI ? 2026 l or the timeout. */
    allow_present = app_allow_present(app, true);
    if (app->needs_render && allow_present) {
      if (app->sync_held_frame) {
        terminal_buffer_mark_all_dirty(app->term_buffer);
        renderer_force_full_redraw(app->renderer);
        app->sync_held_frame = false;
      }
      renderer_render(app->renderer, app->term_buffer, app->mouse_manager);
      app->needs_render = false;
    } else if (app->needs_render && !allow_present) {
      app->sync_held_frame = true;
    }
  }

  log_debug("Exiting main loop");
}

/* ========================================================================
 * Cleanup
 * ======================================================================== */

/**
 * Cleanup and destroy all resources (safe for partial init / goto-fail).
 */
static void app_cleanup(AppState *app) {
  if (!app)
    return;

  log_trace("Cleaning up application");

  app_search_destroy(&app->search);

  if (app->mouse_manager) {
    mouse_manager_destroy(app->mouse_manager);
    app->mouse_manager = nullptr;
  }

  if (app->pty_manager) {
    pty_manager_destroy(app->pty_manager);
    app->pty_manager = nullptr;
  }

  if (app->ansi_parser) {
    ansi_parser_destroy(app->ansi_parser);
    app->ansi_parser = nullptr;
  }

  if (app->renderer) {
    renderer_destroy(app->renderer);
    app->renderer = nullptr;
  }

  if (app->term_buffer) {
    terminal_buffer_destroy(app->term_buffer);
    app->term_buffer = nullptr;
  }

  if (app->font_manager) {
    font_manager_destroy(app->font_manager);
    app->font_manager = nullptr;
  }

  if (app->sdl_renderer) {
    SDL_DestroyRenderer(app->sdl_renderer);
    app->sdl_renderer = nullptr;
  }

  if (app->window) {
    SDL_DestroyWindow(app->window);
    app->window = nullptr;
  }

  if (app->sdl_initialized) {
    if (TTF_WasInit())
      TTF_Quit();
    SDL_Quit();
    app->sdl_initialized = false;
  }

  if (app->config) {
    config_destroy(app->config);
    app->config = nullptr;
  }

  log_debug("Cleanup complete");
}

/* ========================================================================
 * Entry Point
 * ======================================================================== */

int main(int argc, char *argv[]) {
  printf("mitr v%s - Terminal Emulator\n", MITR_VERSION);

#ifdef DEBUG
  /* LOG_DEBUG avoids per-byte trace spam in ansi_parser which kills
   * performance with syntax-highlighting apps (Helix, etc.) */
  log_set_level(LOG_DEBUG);
#else
  log_set_level(LOG_INFO);
#endif

  /* Parse command line arguments */
  const char *config_path = nullptr;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      config_path = argv[i + 1];
      i++; /* Skip next argument */
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Usage: %s [OPTIONS]\n", argv[0]);
      printf("Options:\n");
      printf("  --config <path>  Path to config file\n");
      printf("  --help, -h       Show this help message\n");
      printf("\n");
      printf("Config file locations (in order of priority):\n");
      printf("  1. --config <path> argument\n");
      printf("  2. $XDG_CONFIG_HOME/mitr/mitr.yml\n");
      printf("  3. ~/.config/mitr/mitr.yml\n");
      printf("  4. Built-in defaults\n");
      return EXIT_SUCCESS;
    }
  }

  AppState app = {0};

  /* Load configuration */
  if (config_path) {
    log_debug("Loading config from: %s", config_path);
    app.config = config_load(config_path);
  } else {
    log_debug("Loading config from default paths");
    app.config = config_load_default_path();
  }

  if (!app.config) {
    fprintf(stderr, "ERROR: Failed to load configuration\n");
    return EXIT_FAILURE;
  }

  log_debug("Config loaded: window=%dx%d, font=%s(%d), shell=%s",
            app.config->window_width, app.config->window_height,
            app.config->font_family, app.config->font_size,
            app.config->shell[0] ? app.config->shell : "$SHELL");

  /* Initialize (failures already run app_cleanup via app_fail) */
  MitrError err = app_init(&app);
  if (err != MITR_OK) {
    fprintf(stderr, "ERROR: Initialization failed: %s (%d)\n",
            mitr_error_string(err), (int)err);
    return EXIT_FAILURE;
  }

  /* Run main loop */
  app_run(&app);

  /* Cleanup */
  app_cleanup(&app);

  printf("\nmitr terminated successfully\n");
  return EXIT_SUCCESS;
}
