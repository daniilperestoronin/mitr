/**
 * config.h - Configuration system for mitr
 *
 * Provides YAML-based configuration with sensible defaults.
 * Config file locations (in priority order):
 *   1. --config <path> command line argument
 *   2. $XDG_CONFIG_HOME/mitr/mitr.yml
 *   3. ~/.config/mitr/mitr.yml
 *   4. Default values (hardcoded fallbacks)
 */

#ifndef MITR_CONFIG_H
#define MITR_CONFIG_H

#include "common.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================================================================
 * Cursor Style
 * ======================================================================== */

typedef enum {
  CURSOR_STYLE_BLOCK,
  CURSOR_STYLE_UNDERLINE,
  CURSOR_STYLE_BAR
} CursorStyle;

/* ========================================================================
 * OSC 52 clipboard writes (PTY → host clipboard)
 * ======================================================================== */

typedef enum {
  OSC52_OFF,     /* drop writes; do not register the parser callback */
  OSC52_CONFIRM, /* yes/no dialog, then write */
  OSC52_ON       /* write immediately (legacy behavior) */
} Osc52Mode;

#define OSC52_PREVIEW_MAX 80

/* ========================================================================
 * Configuration Structure
 * ======================================================================== */

typedef struct {
  /* Window */
  int window_width;
  int window_height;
  bool window_resizable;
  float opacity; /* 0.0 .. 1.0, window opacity */
  int padding_x; /* horizontal padding in pixels */
  int padding_y; /* vertical padding in pixels */

  /* Font */
  char font_path[512];          /* empty = resolve font_family */
  char font_fallback_path[512]; /* empty = native system fallback */
  char font_family[256];        /* primary font family name */
  int font_size;

  /* Colors - 16 ANSI colors palette */
  uint32_t colors[16];      /* color0..color15 (normal 0-7, bright 8-15) */
  uint32_t foreground;      /* default foreground */
  uint32_t background;      /* default background */
  uint32_t cursor_color;    /* cursor color */
  uint32_t selection_color; /* selection highlight */
  uint8_t background_alpha; /* 0..255, background alpha for compositing */

  /* Cursor */
  CursorStyle cursor_style;
  bool cursor_blink;
  int cursor_blink_interval_ms;

  /* Terminal */
  char shell[256];      /* shell path, empty = $SHELL or /bin/sh */
  char term_env[64];    /* TERM env variable, e.g. xterm-256color */
  int scrollback_lines; /* scrollback buffer size */
  bool bold_is_bright;  /* bold text uses bright colors */
  Osc52Mode osc52;      /* OSC 52 clipboard writes: off | confirm | on */
} MitrConfig;

/* ========================================================================
 * Configuration API
 * ======================================================================== */

/**
 * Create configuration with default values
 *
 * Returns: Newly allocated config struct with defaults, or nullptr on failure
 *
 * Note: Caller must free with config_destroy()
 */
MitrConfig *config_create_default(void);

/**
 * Load configuration from YAML file
 *
 * @param path: Path to YAML config file
 *
 * Returns: Loaded config (with defaults for missing values), or nullptr on
 * error
 *
 * Note: Invalid values in config are logged as warnings and replaced with
 * defaults Note: If file doesn't exist, returns default config (no error)
 * Note: Caller must free with config_destroy()
 */
MitrConfig *config_load(const char *path);

/**
 * Find and load configuration file using standard search paths
 *
 * Searches in order:
 *   1. $XDG_CONFIG_HOME/mitr/mitr.yml
 *   2. ~/.config/mitr/mitr.yml
 *   3. Returns defaults if no file found
 *
 * Returns: Loaded config, or defaults if no config file found
 *
 * Note: Caller must free with config_destroy()
 */
MitrConfig *config_load_default_path(void);

/**
 * Destroy configuration and free memory
 *
 * @param config: Config to destroy (can be nullptr)
 */
void config_destroy(MitrConfig *config);

/**
 * Parse hex color string to uint32_t RGB
 *
 * @param hex: Hex color string in format "#RRGGBB" or "RRGGBB"
 * @param out: Output color value
 *
 * Returns: MITR_OK on success, MITR_ERROR_INVALID_PARAM on invalid format
 *
 * Example: parse_hex_color("#FF0000", &color) -> color = 0x00FF0000
 */
MitrError parse_hex_color(const char *hex, uint32_t *out);

/**
 * Get cursor style from string
 *
 * @param str: String representation ("block", "underline", "bar")
 * @param out: Output cursor style
 *
 * Returns: MITR_OK on success, MITR_ERROR_INVALID_PARAM on invalid string
 */
MitrError parse_cursor_style(const char *str, CursorStyle *out);

/**
 * Get OSC 52 mode from string
 *
 * @param str: "off", "confirm", or "on" (case-insensitive)
 * @param out: Output mode
 *
 * Returns: MITR_OK on success, MITR_ERROR_INVALID_PARAM on invalid string
 */
MitrError parse_osc52_mode(const char *str, Osc52Mode *out);

/**
 * Whether an OSC 52 clipboard write should be applied.
 *
 * `off` always drops. `on` always applies. `confirm` applies only when
 * `user_accepted` is true. SDL-free so tests can cover app policy.
 */
bool osc52_should_apply(Osc52Mode mode, bool user_accepted);

/**
 * Confirm-dialog body: payload length plus a preview of at most
 * OSC52_PREVIEW_MAX characters (never the full payload when longer).
 *
 * Returns bytes written excluding NUL, or 0 on invalid args.
 */
size_t osc52_format_confirm_message(const char *text, size_t len, char *out,
                                    size_t out_size);

#endif /* MITR_CONFIG_H */
