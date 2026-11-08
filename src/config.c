/**
 * config.c - Configuration system implementation
 *
 * YAML-based configuration with sensible defaults.
 * Uses libyaml for parsing YAML files.
 */

#include "config.h"
#include "common.h"
#include "pty_manager.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yaml.h>

/* ========================================================================
 * Default Values
 * ======================================================================== */

#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 600
#define DEFAULT_WINDOW_RESIZABLE true
#define DEFAULT_OPACITY 1.0f
#define DEFAULT_PADDING_X 2
#define DEFAULT_PADDING_Y 2

#define DEFAULT_FONT_FAMILY "JetBrains Mono"
#define DEFAULT_FONT_SIZE 16

#define DEFAULT_CURSOR_STYLE CURSOR_STYLE_BLOCK
#define DEFAULT_CURSOR_BLINK false
#define DEFAULT_CURSOR_BLINK_INTERVAL 500

#define DEFAULT_TERM_ENV "xterm-256color"
#define DEFAULT_SCROLLBACK_LINES 10000
#define DEFAULT_BOLD_IS_BRIGHT true
#define DEFAULT_OSC52 OSC52_OFF

#define MAX_SECTION_DEPTH 4

/* ========================================================================
 * Helpers
 * ======================================================================== */

static bool file_exists(const char *path) {
  if (!path)
    return false;
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void expand_home_dir(const char *path, char *out, size_t out_size) {
  if (!path || !out || out_size == 0)
    return;

  if (path[0] == '~' && path[1] == '/') {
    const char *home = getenv("HOME");
    if (home) {
      snprintf(out, out_size, "%s%s", home, path + 1);
      return;
    }
  }

  snprintf(out, out_size, "%s", path);
}

static void str_tolower(char *str) {
  if (!str)
    return;
  for (size_t i = 0; str[i]; i++) {
    str[i] = (char)tolower((unsigned char)str[i]);
  }
}

static void copy_cstr(char *dst, size_t dst_size, const char *src) {
  if (!dst || dst_size == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, dst_size, "%s", src);
}

static bool parse_int_strict(const char *s, long min, long max, long *out) {
  if (!s || !out || !*s)
    return false;

  errno = 0;
  char *end = nullptr;
  long v = strtol(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0')
    return false;
  if (v < min || v > max)
    return false;
  *out = v;
  return true;
}

static bool parse_float_strict(const char *s, float min, float max,
                               float *out) {
  if (!s || !out || !*s)
    return false;

  errno = 0;
  char *end = nullptr;
  float v = strtof(s, &end);
  if (errno != 0 || end == s || *end != '\0')
    return false;
  if (v < min || v > max)
    return false;
  *out = v;
  return true;
}

static bool parse_bool_strict(const char *s, bool *out) {
  if (!s || !out)
    return false;
  if (strcmp(s, "true") == 0 || strcmp(s, "yes") == 0 || strcmp(s, "1") == 0) {
    *out = true;
    return true;
  }
  if (strcmp(s, "false") == 0 || strcmp(s, "no") == 0 || strcmp(s, "0") == 0) {
    *out = false;
    return true;
  }
  return false;
}

static int ansi_color_index(const char *key) {
  static const struct {
    const char *name;
    int index;
  } table[] = {
      {"black", 0},         {"red", 1},
      {"green", 2},         {"yellow", 3},
      {"blue", 4},          {"magenta", 5},
      {"cyan", 6},          {"white", 7},
      {"bright_black", 8},  {"bright_red", 9},
      {"bright_green", 10}, {"bright_yellow", 11},
      {"bright_blue", 12},  {"bright_magenta", 13},
      {"bright_cyan", 14},  {"bright_white", 15},
      {"color0", 0},        {"color1", 1},
      {"color2", 2},        {"color3", 3},
      {"color4", 4},        {"color5", 5},
      {"color6", 6},        {"color7", 7},
      {"color8", 8},        {"color9", 9},
      {"color10", 10},      {"color11", 11},
      {"color12", 12},      {"color13", 13},
      {"color14", 14},      {"color15", 15},
  };

  for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
    if (strcmp(key, table[i].name) == 0)
      return table[i].index;
  }
  return -1;
}

/* ========================================================================
 * Parsing Functions
 * ======================================================================== */

MitrError parse_hex_color(const char *hex, uint32_t *out) {
  if (!hex || !out)
    return MITR_ERROR_INVALID_PARAM;

  if (hex[0] == '#')
    hex++;

  size_t len = strlen(hex);
  if (len != 6)
    return MITR_ERROR_INVALID_PARAM;

  for (size_t i = 0; i < len; i++) {
    if (!isxdigit((unsigned char)hex[i]))
      return MITR_ERROR_INVALID_PARAM;
  }

  errno = 0;
  char *endptr = nullptr;
  unsigned long value = strtoul(hex, &endptr, 16);
  if (errno != 0 || endptr == hex || *endptr != '\0' || value > 0xFFFFFFul)
    return MITR_ERROR_INVALID_PARAM;

  uint8_t r = (uint8_t)((value >> 16) & 0xFF);
  uint8_t g = (uint8_t)((value >> 8) & 0xFF);
  uint8_t b = (uint8_t)(value & 0xFF);
  *out = RGB(r, g, b);
  return MITR_OK;
}

MitrError parse_cursor_style(const char *str, CursorStyle *out) {
  if (!str || !out)
    return MITR_ERROR_INVALID_PARAM;

  char lower[64];
  copy_cstr(lower, sizeof(lower), str);
  str_tolower(lower);

  if (strcmp(lower, "block") == 0) {
    *out = CURSOR_STYLE_BLOCK;
    return MITR_OK;
  }
  if (strcmp(lower, "underline") == 0) {
    *out = CURSOR_STYLE_UNDERLINE;
    return MITR_OK;
  }
  if (strcmp(lower, "bar") == 0) {
    *out = CURSOR_STYLE_BAR;
    return MITR_OK;
  }
  return MITR_ERROR_INVALID_PARAM;
}

MitrError parse_osc52_mode(const char *str, Osc52Mode *out) {
  if (!str || !out)
    return MITR_ERROR_INVALID_PARAM;

  char lower[64];
  copy_cstr(lower, sizeof(lower), str);
  str_tolower(lower);

  if (strcmp(lower, "off") == 0) {
    *out = OSC52_OFF;
    return MITR_OK;
  }
  if (strcmp(lower, "confirm") == 0) {
    *out = OSC52_CONFIRM;
    return MITR_OK;
  }
  if (strcmp(lower, "on") == 0) {
    *out = OSC52_ON;
    return MITR_OK;
  }
  return MITR_ERROR_INVALID_PARAM;
}

bool osc52_should_apply(Osc52Mode mode, bool user_accepted) {
  switch (mode) {
  case OSC52_ON:
    return true;
  case OSC52_CONFIRM:
    return user_accepted;
  case OSC52_OFF:
  default:
    return false;
  }
}

size_t osc52_format_confirm_message(const char *text, size_t len, char *out,
                                    size_t out_size) {
  char preview[OSC52_PREVIEW_MAX + 1];
  size_t preview_len;
  int n;

  if (!out || out_size == 0)
    return 0;
  out[0] = '\0';
  if (len > 0 && !text)
    return 0;

  preview_len = len;
  if (preview_len > OSC52_PREVIEW_MAX)
    preview_len = OSC52_PREVIEW_MAX;
  if (preview_len > 0)
    memcpy(preview, text, preview_len);
  preview[preview_len] = '\0';

  n = snprintf(out, out_size, "Allow OSC 52 clipboard write (%zu bytes): %s",
               len, preview);
  if (n < 0) {
    out[0] = '\0';
    return 0;
  }
  if ((size_t)n >= out_size)
    return out_size - 1;
  return (size_t)n;
}

/* ========================================================================
 * Default Configuration
 * ======================================================================== */

MitrConfig *config_create_default(void) {
  MitrConfig *cfg = checked_calloc(1, sizeof(MitrConfig));
  if (!cfg)
    return nullptr;

  cfg->window_width = DEFAULT_WINDOW_WIDTH;
  cfg->window_height = DEFAULT_WINDOW_HEIGHT;
  cfg->window_resizable = DEFAULT_WINDOW_RESIZABLE;
  cfg->opacity = DEFAULT_OPACITY;
  cfg->padding_x = DEFAULT_PADDING_X;
  cfg->padding_y = DEFAULT_PADDING_Y;

  copy_cstr(cfg->font_family, sizeof(cfg->font_family), DEFAULT_FONT_FAMILY);
  cfg->font_path[0] = '\0';
  cfg->font_fallback_path[0] = '\0';
  cfg->font_size = DEFAULT_FONT_SIZE;

  cfg->colors[0] = COLOR_BLACK;
  cfg->colors[1] = COLOR_RED;
  cfg->colors[2] = COLOR_GREEN;
  cfg->colors[3] = COLOR_YELLOW;
  cfg->colors[4] = COLOR_BLUE;
  cfg->colors[5] = COLOR_MAGENTA;
  cfg->colors[6] = COLOR_CYAN;
  cfg->colors[7] = COLOR_WHITE;
  cfg->colors[8] = COLOR_BRIGHT_BLACK;
  cfg->colors[9] = COLOR_BRIGHT_RED;
  cfg->colors[10] = COLOR_BRIGHT_GREEN;
  cfg->colors[11] = COLOR_BRIGHT_YELLOW;
  cfg->colors[12] = COLOR_BRIGHT_BLUE;
  cfg->colors[13] = COLOR_BRIGHT_MAGENTA;
  cfg->colors[14] = COLOR_BRIGHT_CYAN;
  cfg->colors[15] = COLOR_BRIGHT_WHITE;

  cfg->foreground = COLOR_DEFAULT_FG;
  cfg->background = COLOR_DEFAULT_BG;
  cfg->cursor_color = RGB(192, 192, 192);
  cfg->selection_color = RGB(100, 150, 255);
  cfg->background_alpha = 255;

  cfg->cursor_style = DEFAULT_CURSOR_STYLE;
  cfg->cursor_blink = DEFAULT_CURSOR_BLINK;
  cfg->cursor_blink_interval_ms = DEFAULT_CURSOR_BLINK_INTERVAL;

  cfg->shell[0] = '\0';
  copy_cstr(cfg->term_env, sizeof(cfg->term_env), DEFAULT_TERM_ENV);
  cfg->scrollback_lines = DEFAULT_SCROLLBACK_LINES;
  cfg->bold_is_bright = DEFAULT_BOLD_IS_BRIGHT;
  cfg->osc52 = DEFAULT_OSC52;

  return cfg;
}

/* ========================================================================
 * YAML Configuration Loading
 * ======================================================================== */

static void warn_unknown_key(const char *section, const char *key) {
  if (section && section[0])
    log_warn("Unknown config key: %s.%s", section, key);
  else
    log_warn("Unknown config key: %s", key);
}

static void warn_invalid_value(const char *section, const char *key,
                               const char *value) {
  if (section && section[0])
    log_warn("Invalid config value for %s.%s: '%s'", section, key, value);
  else
    log_warn("Invalid config value for %s: '%s'", key, value);
}

static void apply_config_value(MitrConfig *cfg, const char *section,
                               const char *key, const char *value) {
  if (!cfg || !key || !value)
    return;

  if (strcmp(section, "window") == 0) {
    if (strcmp(key, "width") == 0) {
      long v;
      if (parse_int_strict(value, 1, MITR_MAX_WINDOW_WIDTH, &v))
        cfg->window_width = (int)v;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "height") == 0) {
      long v;
      if (parse_int_strict(value, 1, MITR_MAX_WINDOW_HEIGHT, &v))
        cfg->window_height = (int)v;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "opacity") == 0) {
      float v;
      if (parse_float_strict(value, 0.0f, 1.0f, &v))
        cfg->opacity = v;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "resizable") == 0) {
      bool v;
      if (parse_bool_strict(value, &v))
        cfg->window_resizable = v;
      else
        warn_invalid_value(section, key, value);
    } else {
      warn_unknown_key(section, key);
    }
    return;
  }

  if (strcmp(section, "window.padding") == 0) {
    if (strcmp(key, "x") == 0) {
      long v;
      if (parse_int_strict(value, 0, 1024, &v))
        cfg->padding_x = (int)v;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "y") == 0) {
      long v;
      if (parse_int_strict(value, 0, 1024, &v))
        cfg->padding_y = (int)v;
      else
        warn_invalid_value(section, key, value);
    } else {
      warn_unknown_key(section, key);
    }
    return;
  }

  if (strcmp(section, "font") == 0) {
    if (strcmp(key, "family") == 0) {
      copy_cstr(cfg->font_family, sizeof(cfg->font_family), value);
    } else if (strcmp(key, "path") == 0) {
      copy_cstr(cfg->font_path, sizeof(cfg->font_path), value);
    } else if (strcmp(key, "fallback_path") == 0) {
      copy_cstr(cfg->font_fallback_path, sizeof(cfg->font_fallback_path),
                value);
    } else if (strcmp(key, "size") == 0) {
      long v;
      if (parse_int_strict(value, 1, 512, &v))
        cfg->font_size = (int)v;
      else
        warn_invalid_value(section, key, value);
    } else {
      warn_unknown_key(section, key);
    }
    return;
  }

  if (strcmp(section, "cursor") == 0) {
    if (strcmp(key, "style") == 0) {
      CursorStyle style;
      if (parse_cursor_style(value, &style) == MITR_OK)
        cfg->cursor_style = style;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "blink") == 0) {
      bool v;
      if (parse_bool_strict(value, &v))
        cfg->cursor_blink = v;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "blink_interval") == 0) {
      long v;
      if (parse_int_strict(value, 1, 60000, &v))
        cfg->cursor_blink_interval_ms = (int)v;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "color") == 0) {
      uint32_t color;
      if (parse_hex_color(value, &color) == MITR_OK)
        cfg->cursor_color = color;
      else
        warn_invalid_value(section, key, value);
    } else {
      warn_unknown_key(section, key);
    }
    return;
  }

  if (strcmp(section, "colors") == 0) {
    uint32_t color;
    if (strcmp(key, "foreground") == 0) {
      if (parse_hex_color(value, &color) == MITR_OK)
        cfg->foreground = color;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "background") == 0) {
      if (parse_hex_color(value, &color) == MITR_OK)
        cfg->background = color;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "selection") == 0) {
      if (parse_hex_color(value, &color) == MITR_OK)
        cfg->selection_color = color;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "cursor") == 0) {
      if (parse_hex_color(value, &color) == MITR_OK)
        cfg->cursor_color = color;
      else
        warn_invalid_value(section, key, value);
    } else {
      int idx = ansi_color_index(key);
      if (idx >= 0) {
        if (parse_hex_color(value, &color) == MITR_OK)
          cfg->colors[idx] = color;
        else
          warn_invalid_value(section, key, value);
      } else {
        warn_unknown_key(section, key);
      }
    }
    return;
  }

  if (strcmp(section, "terminal") == 0) {
    if (strcmp(key, "shell") == 0) {
      if (value[0] == '\0') {
        cfg->shell[0] = '\0';
      } else if (pty_manager_shell_path_valid(value)) {
        copy_cstr(cfg->shell, sizeof(cfg->shell), value);
      } else {
        warn_invalid_value(section, key, value);
      }
    } else if (strcmp(key, "term") == 0) {
      copy_cstr(cfg->term_env, sizeof(cfg->term_env), value);
    } else if (strcmp(key, "scrollback_lines") == 0) {
      long v;
      if (parse_int_strict(value, 0, (long)MITR_MAX_SCROLLBACK, &v))
        cfg->scrollback_lines = (int)v;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "bold_is_bright") == 0) {
      bool v;
      if (parse_bool_strict(value, &v))
        cfg->bold_is_bright = v;
      else
        warn_invalid_value(section, key, value);
    } else if (strcmp(key, "osc52") == 0) {
      Osc52Mode mode;
      if (parse_osc52_mode(value, &mode) == MITR_OK)
        cfg->osc52 = mode;
      else
        warn_invalid_value(section, key, value);
    } else {
      warn_unknown_key(section, key);
    }
    return;
  }

  if (section[0] == '\0') {
    warn_unknown_key(nullptr, key);
  } else {
    warn_unknown_key(section, key);
  }
}

static bool section_is_known(const char *section) {
  return strcmp(section, "window") == 0 ||
         strcmp(section, "window.padding") == 0 ||
         strcmp(section, "font") == 0 || strcmp(section, "cursor") == 0 ||
         strcmp(section, "colors") == 0 || strcmp(section, "terminal") == 0;
}

static MitrConfig *parse_yaml_file(const char *path) {
  if (!path)
    return nullptr;

  MitrConfig *cfg = config_create_default();
  if (!cfg)
    return nullptr;

  FILE *file = fopen(path, "r");
  if (!file) {
    log_debug("Config file not found: %s (using defaults)", path);
    return cfg;
  }

  yaml_parser_t parser;
  yaml_event_t event;

  if (!yaml_parser_initialize(&parser)) {
    log_warn("Failed to initialize YAML parser");
    fclose(file);
    return cfg;
  }

  yaml_parser_set_input_file(&parser, file);

  char section_stack[MAX_SECTION_DEPTH][64];
  int depth = 0;
  char pending_key[64] = {0};
  bool stream_ok = true;

  do {
    if (!yaml_parser_parse(&parser, &event)) {
      log_warn("YAML parse error at line %zu", parser.problem_mark.line + 1);
      stream_ok = false;
      break;
    }

    switch (event.type) {
    case YAML_MAPPING_START_EVENT:
      if (pending_key[0]) {
        if (depth < MAX_SECTION_DEPTH) {
          if (depth == 0) {
            copy_cstr(section_stack[depth], sizeof(section_stack[depth]),
                      pending_key);
          } else {
            /* Join outside the stack before applying its path length limit. */
            char section[sizeof(section_stack[0]) * 2];
            snprintf(section, sizeof(section), "%s.%s",
                     section_stack[depth - 1], pending_key);
            copy_cstr(section_stack[depth], sizeof(section_stack[depth]),
                      section);
          }
          if (!section_is_known(section_stack[depth]) &&
              strcmp(pending_key, "padding") != 0) {
            /* Allow entering known nested padding; warn for unknown maps */
            if (depth == 0 ||
                (strcmp(section_stack[depth - 1], "window") != 0 ||
                 strcmp(pending_key, "padding") != 0)) {
              if (!section_is_known(section_stack[depth]))
                warn_unknown_key(depth > 0 ? section_stack[depth - 1] : nullptr,
                                 pending_key);
            }
          }
          depth++;
        }
        pending_key[0] = '\0';
      } else if (depth == 0) {
        /* Document root mapping */
      }
      break;

    case YAML_MAPPING_END_EVENT:
      if (depth > 0)
        depth--;
      break;

    case YAML_SEQUENCE_START_EVENT:
    case YAML_SEQUENCE_END_EVENT:
      /* Sequences are not used in mitr.yml — ignore content via key reset */
      pending_key[0] = '\0';
      break;

    case YAML_SCALAR_EVENT: {
      const char *value = (const char *)event.data.scalar.value;
      if (!pending_key[0]) {
        copy_cstr(pending_key, sizeof(pending_key), value);
      } else {
        const char *section = depth > 0 ? section_stack[depth - 1] : "";
        apply_config_value(cfg, section, pending_key, value);
        pending_key[0] = '\0';
      }
      break;
    }

    default:
      break;
    }

    if (event.type != YAML_STREAM_END_EVENT)
      yaml_event_delete(&event);
  } while (stream_ok && event.type != YAML_STREAM_END_EVENT);

  if (stream_ok)
    yaml_event_delete(&event);

  yaml_parser_delete(&parser);
  fclose(file);

  log_debug("Config loaded from: %s", path);
  return cfg;
}

MitrConfig *config_load(const char *path) {
  if (!path)
    return config_create_default();

  char expanded_path[1024];
  expand_home_dir(path, expanded_path, sizeof(expanded_path));
  return parse_yaml_file(expanded_path);
}

MitrConfig *config_load_default_path(void) {
  const char *xdg_config = getenv("XDG_CONFIG_HOME");
  if (xdg_config) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/mitr/mitr.yml", xdg_config);
    if (file_exists(path)) {
      log_debug("Found config at: %s", path);
      return config_load(path);
    }
  }

  const char *home = getenv("HOME");
  if (home) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/.config/mitr/mitr.yml", home);
    if (file_exists(path)) {
      log_debug("Found config at: %s", path);
      return config_load(path);
    }
  }

  log_debug("No config file found, using defaults");
  return config_create_default();
}

void config_destroy(MitrConfig *config) {
  if (config)
    free(config);
}
