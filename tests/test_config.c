/**
 * test_config.c - Tests for Configuration System
 *
 * Following TDD approach - tests define expected behavior
 */

/* Expose mkdtemp on macOS when the test build enables POSIX declarations. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "config.h"
#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/* ========================================================================
 * Default Configuration Tests
 * ======================================================================== */

TEST(config_create_default_returns_valid_config) {
  MitrConfig *cfg = config_create_default();

  ASSERT_NOT_NULL(cfg);

  /* Should have sensible default values */
  ASSERT_EQ(cfg->window_width, 800);
  ASSERT_EQ(cfg->window_height, 600);
  ASSERT_TRUE(cfg->window_resizable);
  ASSERT_EQ((int)(cfg->opacity * 10), 10); /* opacity = 1.0 */

  config_destroy(cfg);
}

TEST(config_create_default_has_default_font) {
  MitrConfig *cfg = config_create_default();

  ASSERT_NOT_NULL(cfg);
  ASSERT_EQ(cfg->font_size, 16);
  ASSERT_STR_EQ(cfg->font_family, "JetBrains Mono");
  ASSERT_STR_EQ(cfg->font_path, "");
  ASSERT_STR_EQ(cfg->font_fallback_path, "");

  config_destroy(cfg);
}

TEST(config_create_default_has_16_ansi_colors) {
  MitrConfig *cfg = config_create_default();

  ASSERT_NOT_NULL(cfg);

  /* Check that all 16 ANSI colors are defined */
  ASSERT_EQ(cfg->colors[0], COLOR_BLACK);         /* black */
  ASSERT_EQ(cfg->colors[1], COLOR_RED);           /* red */
  ASSERT_EQ(cfg->colors[7], COLOR_WHITE);         /* white */
  ASSERT_EQ(cfg->colors[15], COLOR_BRIGHT_WHITE); /* bright white */

  /* Check default FG/BG */
  ASSERT_EQ(cfg->foreground, COLOR_DEFAULT_FG);
  ASSERT_EQ(cfg->background, COLOR_DEFAULT_BG);

  config_destroy(cfg);
}

TEST(config_create_default_has_cursor_settings) {
  MitrConfig *cfg = config_create_default();

  ASSERT_NOT_NULL(cfg);
  ASSERT_EQ(cfg->cursor_style, CURSOR_STYLE_BLOCK);
  ASSERT_FALSE(
      cfg->cursor_blink); /* Blinking disabled by default for performance */
  ASSERT_EQ(cfg->cursor_blink_interval_ms, 500);

  config_destroy(cfg);
}

TEST(config_create_default_has_terminal_settings) {
  MitrConfig *cfg = config_create_default();

  ASSERT_NOT_NULL(cfg);
  ASSERT_STR_EQ(cfg->term_env, "xterm-256color");
  ASSERT_EQ(cfg->scrollback_lines, 10000);
  ASSERT_TRUE(cfg->bold_is_bright);
  ASSERT_EQ(cfg->osc52, OSC52_OFF);

  config_destroy(cfg);
}

TEST(config_destroy_handles_null) {
  /* Should not crash */
  config_destroy(nullptr);
  ASSERT_TRUE(true);
}

/* ========================================================================
 * Color Parsing Tests
 * ======================================================================== */

TEST(parse_hex_color_with_hash) {
  uint32_t color = 0;

  ASSERT_EQ(parse_hex_color("#FF0000", &color), MITR_OK);
  ASSERT_EQ(color, RGB(255, 0, 0));

  ASSERT_EQ(parse_hex_color("#00FF00", &color), MITR_OK);
  ASSERT_EQ(color, RGB(0, 255, 0));

  ASSERT_EQ(parse_hex_color("#0000FF", &color), MITR_OK);
  ASSERT_EQ(color, RGB(0, 0, 255));
}

TEST(parse_hex_color_without_hash) {
  uint32_t color = 0;

  ASSERT_EQ(parse_hex_color("FF0000", &color), MITR_OK);
  ASSERT_EQ(color, RGB(255, 0, 0));

  ASSERT_EQ(parse_hex_color("FFFFFF", &color), MITR_OK);
  ASSERT_EQ(color, RGB(255, 255, 255));
}

TEST(parse_hex_color_lowercase) {
  uint32_t color = 0;

  ASSERT_EQ(parse_hex_color("#ff0000", &color), MITR_OK);
  ASSERT_EQ(color, RGB(255, 0, 0));

  ASSERT_EQ(parse_hex_color("abcdef", &color), MITR_OK);
  ASSERT_EQ(color, RGB(171, 205, 239));
}

TEST(parse_hex_color_invalid_format) {
  uint32_t color = 0;

  /* Too short */
  ASSERT_EQ(parse_hex_color("#FF", &color), MITR_ERROR_INVALID_PARAM);

  /* Too long */
  ASSERT_EQ(parse_hex_color("#FF00001", &color), MITR_ERROR_INVALID_PARAM);

  /* Invalid characters */
  ASSERT_EQ(parse_hex_color("#GGGGGG", &color), MITR_ERROR_INVALID_PARAM);

  /* nullptr input */
  ASSERT_EQ(parse_hex_color(nullptr, &color), MITR_ERROR_INVALID_PARAM);
}

/* ========================================================================
 * Cursor Style Parsing Tests
 * ======================================================================== */

TEST(parse_cursor_style_valid) {
  CursorStyle style;

  ASSERT_EQ(parse_cursor_style("block", &style), MITR_OK);
  ASSERT_EQ(style, CURSOR_STYLE_BLOCK);

  ASSERT_EQ(parse_cursor_style("underline", &style), MITR_OK);
  ASSERT_EQ(style, CURSOR_STYLE_UNDERLINE);

  ASSERT_EQ(parse_cursor_style("bar", &style), MITR_OK);
  ASSERT_EQ(style, CURSOR_STYLE_BAR);
}

TEST(parse_cursor_style_case_insensitive) {
  CursorStyle style;

  ASSERT_EQ(parse_cursor_style("BLOCK", &style), MITR_OK);
  ASSERT_EQ(style, CURSOR_STYLE_BLOCK);

  ASSERT_EQ(parse_cursor_style("UnderLine", &style), MITR_OK);
  ASSERT_EQ(style, CURSOR_STYLE_UNDERLINE);
}

TEST(parse_cursor_style_invalid) {
  CursorStyle style;

  ASSERT_EQ(parse_cursor_style("invalid", &style), MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(parse_cursor_style("", &style), MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(parse_cursor_style(nullptr, &style), MITR_ERROR_INVALID_PARAM);
}

/* ========================================================================
 * OSC 52 mode and policy (no SDL)
 * ======================================================================== */

TEST(parse_osc52_mode_valid) {
  Osc52Mode mode;

  ASSERT_EQ(parse_osc52_mode("off", &mode), MITR_OK);
  ASSERT_EQ(mode, OSC52_OFF);

  ASSERT_EQ(parse_osc52_mode("confirm", &mode), MITR_OK);
  ASSERT_EQ(mode, OSC52_CONFIRM);

  ASSERT_EQ(parse_osc52_mode("on", &mode), MITR_OK);
  ASSERT_EQ(mode, OSC52_ON);
}

TEST(parse_osc52_mode_case_insensitive) {
  Osc52Mode mode;

  ASSERT_EQ(parse_osc52_mode("OFF", &mode), MITR_OK);
  ASSERT_EQ(mode, OSC52_OFF);

  ASSERT_EQ(parse_osc52_mode("Confirm", &mode), MITR_OK);
  ASSERT_EQ(mode, OSC52_CONFIRM);

  ASSERT_EQ(parse_osc52_mode("On", &mode), MITR_OK);
  ASSERT_EQ(mode, OSC52_ON);
}

TEST(parse_osc52_mode_invalid) {
  Osc52Mode mode;

  ASSERT_EQ(parse_osc52_mode("always", &mode), MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(parse_osc52_mode("true", &mode), MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(parse_osc52_mode("", &mode), MITR_ERROR_INVALID_PARAM);
  ASSERT_EQ(parse_osc52_mode(nullptr, &mode), MITR_ERROR_INVALID_PARAM);
}

TEST(osc52_should_apply_off_drops) {
  ASSERT_FALSE(osc52_should_apply(OSC52_OFF, false));
  ASSERT_FALSE(osc52_should_apply(OSC52_OFF, true));
}

TEST(osc52_should_apply_on_writes) {
  ASSERT_TRUE(osc52_should_apply(OSC52_ON, false));
  ASSERT_TRUE(osc52_should_apply(OSC52_ON, true));
}

TEST(osc52_should_apply_confirm_follows_user) {
  ASSERT_FALSE(osc52_should_apply(OSC52_CONFIRM, false));
  ASSERT_TRUE(osc52_should_apply(OSC52_CONFIRM, true));
}

TEST(osc52_format_confirm_message_includes_length_and_preview) {
  char buf[256];
  size_t n = osc52_format_confirm_message("hello", 5, buf, sizeof(buf));

  ASSERT_TRUE(n > 0);
  ASSERT_TRUE(strstr(buf, "5") != nullptr);
  ASSERT_TRUE(strstr(buf, "hello") != nullptr);
}

TEST(osc52_format_confirm_message_truncates_preview) {
  char payload[92];
  char buf[256];
  size_t n;

  memset(payload, 'A', 80);
  memcpy(payload + 80, "UNIQUE_TAIL", 11);
  payload[91] = '\0';

  n = osc52_format_confirm_message(payload, 91, buf, sizeof(buf));
  ASSERT_TRUE(n > 0);
  ASSERT_TRUE(strstr(buf, "91") != nullptr);
  ASSERT_NULL(strstr(buf, "UNIQUE_TAIL"));
}

TEST(config_load_osc52_modes) {
  const char *temp_path = "/tmp/mitr_test_osc52.yml";
  FILE *f = fopen(temp_path, "w");
  if (f) {
    fprintf(f, "terminal:\n");
    fprintf(f, "  osc52: confirm\n");
    fclose(f);
  }

  MitrConfig *cfg = config_load(temp_path);
  ASSERT_NOT_NULL(cfg);
  ASSERT_EQ(cfg->osc52, OSC52_CONFIRM);
  config_destroy(cfg);
  remove(temp_path);

  f = fopen(temp_path, "w");
  if (f) {
    fprintf(f, "terminal:\n");
    fprintf(f, "  osc52: on\n");
    fclose(f);
  }
  cfg = config_load(temp_path);
  ASSERT_NOT_NULL(cfg);
  ASSERT_EQ(cfg->osc52, OSC52_ON);
  config_destroy(cfg);
  remove(temp_path);
}

/* ========================================================================
 * Config File Loading Tests
 * ======================================================================== */

TEST(config_load_nonexistent_file_returns_defaults) {
  /* Loading non-existent file should return defaults without error */
  MitrConfig *cfg = config_load("/tmp/mitr_nonexistent_12345.yml");

  ASSERT_NOT_NULL(cfg);
  ASSERT_EQ(cfg->window_width, 800);
  ASSERT_EQ(cfg->font_size, 16);

  config_destroy(cfg);
}

TEST(config_load_empty_file_returns_defaults) {
  /* Create temporary empty file */
  const char *temp_path = "/tmp/mitr_test_empty.yml";
  FILE *f = fopen(temp_path, "w");
  if (f) {
    fclose(f);
  }

  MitrConfig *cfg = config_load(temp_path);
  ASSERT_NOT_NULL(cfg);
  ASSERT_EQ(cfg->window_width, 800);

  config_destroy(cfg);
  remove(temp_path);
}

TEST(config_load_partial_config_fills_defaults) {
  /* Create config with only window settings */
  const char *temp_path = "/tmp/mitr_test_partial.yml";
  FILE *f = fopen(temp_path, "w");
  if (f) {
    fprintf(f, "window:\n");
    fprintf(f, "  width: 1024\n");
    fprintf(f, "  height: 768\n");
    fclose(f);
  }

  MitrConfig *cfg = config_load(temp_path);
  ASSERT_NOT_NULL(cfg);

  /* Custom values from file */
  ASSERT_EQ(cfg->window_width, 1024);
  ASSERT_EQ(cfg->window_height, 768);

  /* Defaults for missing values */
  ASSERT_EQ(cfg->font_size, 16);
  ASSERT_STR_EQ(cfg->term_env, "xterm-256color");

  config_destroy(cfg);
  remove(temp_path);
}

TEST(config_load_full_config) {
  /* Create complete config file covering sample YAML keys */
  const char *temp_path = "/tmp/mitr_test_full.yml";
  FILE *f = fopen(temp_path, "w");
  if (f) {
    fprintf(f, "window:\n");
    fprintf(f, "  width: 1920\n");
    fprintf(f, "  height: 1080\n");
    fprintf(f, "  opacity: 0.95\n");
    fprintf(f, "  resizable: false\n");
    fprintf(f, "  padding:\n");
    fprintf(f, "    x: 8\n");
    fprintf(f, "    y: 4\n");
    fprintf(f, "font:\n");
    fprintf(f, "  family: \"Fira Code\"\n");
    fprintf(f, "  path: \"/tmp/font.ttf\"\n");
    fprintf(f, "  fallback_path: \"/tmp/fallback.ttf\"\n");
    fprintf(f, "  size: 14\n");
    fprintf(f, "cursor:\n");
    fprintf(f, "  style: underline\n");
    fprintf(f, "  blink: true\n");
    fprintf(f, "  blink_interval: 750\n");
    fprintf(f, "  color: \"#aabbcc\"\n");
    fprintf(f, "colors:\n");
    fprintf(f, "  foreground: \"#ffffff\"\n");
    fprintf(f, "  background: \"#000000\"\n");
    fprintf(f, "  selection: \"#112233\"\n");
    fprintf(f, "  red: \"#cd3131\"\n");
    fprintf(f, "  bright_green: \"#23d18b\"\n");
    fprintf(f, "terminal:\n");
    fprintf(f, "  shell: \"/bin/sh\"\n");
    fprintf(f, "  term: \"xterm-mitr\"\n");
    fprintf(f, "  scrollback_lines: 5000\n");
    fprintf(f, "  bold_is_bright: false\n");
    fprintf(f, "  osc52: confirm\n");
    fclose(f);
  }

  MitrConfig *cfg = config_load(temp_path);
  ASSERT_NOT_NULL(cfg);

  ASSERT_EQ(cfg->window_width, 1920);
  ASSERT_EQ(cfg->window_height, 1080);
  ASSERT_EQ((int)(cfg->opacity * 100), 95);
  ASSERT_FALSE(cfg->window_resizable);
  ASSERT_EQ(cfg->padding_x, 8);
  ASSERT_EQ(cfg->padding_y, 4);
  ASSERT_EQ(cfg->font_size, 14);
  ASSERT_STR_EQ(cfg->font_family, "Fira Code");
  ASSERT_STR_EQ(cfg->font_path, "/tmp/font.ttf");
  ASSERT_STR_EQ(cfg->font_fallback_path, "/tmp/fallback.ttf");
  ASSERT_EQ(cfg->cursor_style, CURSOR_STYLE_UNDERLINE);
  ASSERT_TRUE(cfg->cursor_blink);
  ASSERT_EQ(cfg->cursor_blink_interval_ms, 750);
  ASSERT_EQ(cfg->cursor_color, RGB(0xaa, 0xbb, 0xcc));
  ASSERT_EQ(cfg->selection_color, RGB(0x11, 0x22, 0x33));
  ASSERT_EQ(cfg->colors[1], RGB(0xcd, 0x31, 0x31));
  ASSERT_EQ(cfg->colors[10], RGB(0x23, 0xd1, 0x8b));
  ASSERT_STR_EQ(cfg->shell, "/bin/sh");
  ASSERT_STR_EQ(cfg->term_env, "xterm-mitr");
  ASSERT_EQ(cfg->scrollback_lines, 5000);
  ASSERT_FALSE(cfg->bold_is_bright);
  ASSERT_EQ(cfg->osc52, OSC52_CONFIRM);

  config_destroy(cfg);
  remove(temp_path);
}

TEST(config_load_sample_matches_defaults) {
  MitrConfig *def = config_create_default();
  MitrConfig *cfg = config_load("assets/mitr.yml");
  ASSERT_NOT_NULL(def);
  ASSERT_NOT_NULL(cfg);

  ASSERT_EQ(cfg->window_width, def->window_width);
  ASSERT_EQ(cfg->window_height, def->window_height);
  ASSERT_EQ(cfg->window_resizable, def->window_resizable);
  ASSERT_EQ((int)(cfg->opacity * 100), (int)(def->opacity * 100));
  ASSERT_EQ(cfg->padding_x, def->padding_x);
  ASSERT_EQ(cfg->padding_y, def->padding_y);
  ASSERT_STR_EQ(cfg->font_family, def->font_family);
  ASSERT_STR_EQ(cfg->font_path, def->font_path);
  ASSERT_STR_EQ(cfg->font_fallback_path, def->font_fallback_path);
  ASSERT_EQ(cfg->font_size, def->font_size);
  ASSERT_MEM_EQ(cfg->colors, def->colors, sizeof(def->colors));
  ASSERT_EQ(cfg->foreground, def->foreground);
  ASSERT_EQ(cfg->background, def->background);
  ASSERT_EQ(cfg->cursor_color, def->cursor_color);
  ASSERT_EQ(cfg->selection_color, def->selection_color);
  ASSERT_EQ(cfg->cursor_style, def->cursor_style);
  ASSERT_EQ(cfg->cursor_blink, def->cursor_blink);
  ASSERT_EQ(cfg->cursor_blink_interval_ms, def->cursor_blink_interval_ms);
  ASSERT_STR_EQ(cfg->shell, def->shell);
  ASSERT_STR_EQ(cfg->term_env, def->term_env);
  ASSERT_EQ(cfg->scrollback_lines, def->scrollback_lines);
  ASSERT_EQ(cfg->bold_is_bright, def->bold_is_bright);
  ASSERT_EQ(cfg->osc52, def->osc52);

  config_destroy(cfg);
  config_destroy(def);
}

TEST(config_sample_documents_all_keys) {
  FILE *f = fopen("assets/mitr.yml", "r");
  ASSERT_NOT_NULL(f);

  char buf[65536];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';

  /* Ignore commented palettes so only the live mapping is searched. */
  char *examples = strstr(buf, "Color scheme examples");
  if (examples)
    *examples = '\0';

  const char *keys[] = {
      "\n  width:",
      "\n  height:",
      "\n  opacity:",
      "\n  resizable:",
      "\n    x:",
      "\n    y:",
      "\n  family:",
      "\n  path:",
      "\n  fallback_path:",
      "\n  size:",
      "\n  style:",
      "\n  blink:",
      "\n  blink_interval:",
      "\n  color:",
      "\n  foreground:",
      "\n  background:",
      "\n  selection:",
      "\n  black:",
      "\n  red:",
      "\n  green:",
      "\n  yellow:",
      "\n  blue:",
      "\n  magenta:",
      "\n  cyan:",
      "\n  white:",
      "\n  bright_black:",
      "\n  bright_red:",
      "\n  bright_green:",
      "\n  bright_yellow:",
      "\n  bright_blue:",
      "\n  bright_magenta:",
      "\n  bright_cyan:",
      "\n  bright_white:",
      "\n  shell:",
      "\n  term:",
      "\n  scrollback_lines:",
      "\n  bold_is_bright:",
      "\n  osc52:",
  };

  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    if (!strstr(buf, keys[i])) {
      fprintf(stderr, "sample config missing active key snippet: %s\n",
              keys[i] + 1);
      TEST_FAIL("assets/mitr.yml is missing a configurable key");
    }
  }
}

TEST(config_warns_on_unknown_key) {
  const char *temp_path = "/tmp/mitr_test_unknown.yml";
  FILE *f = fopen(temp_path, "w");
  if (f) {
    fprintf(f, "window:\n");
    fprintf(f, "  width: 640\n");
    fprintf(f, "  not_a_real_key: 123\n");
    fprintf(f, "mystery:\n");
    fprintf(f, "  foo: bar\n");
    fclose(f);
  }

  MitrConfig *cfg = config_load(temp_path);
  ASSERT_NOT_NULL(cfg);
  ASSERT_EQ(cfg->window_width, 640);
  /* Unknown keys must not crash; valid keys still apply */
  config_destroy(cfg);
  remove(temp_path);
}

TEST(config_rejects_non_numeric_garbage) {
  const char *temp_path = "/tmp/mitr_test_garbage.yml";
  FILE *f = fopen(temp_path, "w");
  if (f) {
    fprintf(f, "window:\n");
    fprintf(f, "  width: 12abc\n");
    fprintf(f, "  opacity: 0.5xyz\n");
    fprintf(f, "font:\n");
    fprintf(f, "  size: nope\n");
    fclose(f);
  }

  MitrConfig *cfg = config_load(temp_path);
  ASSERT_NOT_NULL(cfg);
  ASSERT_EQ(cfg->window_width, 800);
  ASSERT_EQ((int)(cfg->opacity * 10), 10);
  ASSERT_EQ(cfg->font_size, 16);

  config_destroy(cfg);
  remove(temp_path);
}

TEST(config_rejects_unsafe_shell_path) {
  const char *temp_path = "/tmp/mitr_test_bad_shell.yml";
  FILE *f = fopen(temp_path, "w");
  if (f) {
    fprintf(f, "terminal:\n");
    fprintf(f, "  shell: \"sh\"\n");
    fclose(f);
  }

  MitrConfig *cfg = config_load(temp_path);
  ASSERT_NOT_NULL(cfg);
  ASSERT_STR_EQ(cfg->shell, "");

  config_destroy(cfg);
  remove(temp_path);
}

TEST(config_load_invalid_values_use_defaults) {
  /* Create config with invalid values */
  const char *temp_path = "/tmp/mitr_test_invalid.yml";
  FILE *f = fopen(temp_path, "w");
  if (f) {
    fprintf(f, "window:\n");
    fprintf(f, "  width: -100\n");  /* Invalid: negative */
    fprintf(f, "  opacity: 2.5\n"); /* Invalid: > 1.0 */
    fprintf(f, "cursor:\n");
    fprintf(f, "  style: invalid_style\n");
    fprintf(f, "colors:\n");
    fprintf(f, "  foreground: \"not_a_color\"\n");
    fprintf(f, "terminal:\n");
    fprintf(f, "  osc52: nope\n");
    fclose(f);
  }

  MitrConfig *cfg = config_load(temp_path);
  ASSERT_NOT_NULL(cfg);

  /* Invalid values should be replaced with defaults */
  ASSERT_EQ(cfg->window_width, 800);                /* default, not -100 */
  ASSERT_EQ((int)(cfg->opacity * 10), 10);          /* 1.0, not 2.5 */
  ASSERT_EQ(cfg->cursor_style, CURSOR_STYLE_BLOCK); /* default */
  ASSERT_EQ(cfg->foreground, COLOR_DEFAULT_FG);     /* default */
  ASSERT_EQ(cfg->osc52, OSC52_OFF);                 /* default */

  config_destroy(cfg);
  remove(temp_path);
}

/* ========================================================================
 * Config Path Resolution Tests
 * ======================================================================== */

TEST(config_load_default_path_returns_config) {
  /* Even if no config file exists, should return defaults */
  MitrConfig *cfg = config_load_default_path();

  ASSERT_NOT_NULL(cfg);
  ASSERT_EQ(cfg->window_width, 800);

  config_destroy(cfg);
}

static bool config_finds_mitr_path(bool use_xdg) {
  char root[] = "/tmp/mitr_config_path_XXXXXX";
  char config_dir[128];
  char app_dir[160];
  char path[192];
  bool found = false;
  if (!mkdtemp(root)) {
    return false;
  }
  snprintf(config_dir, sizeof(config_dir), "%s/.config", root);
  snprintf(app_dir, sizeof(app_dir), "%s/mitr", use_xdg ? root : config_dir);
  snprintf(path, sizeof(path), "%s/mitr.yml", app_dir);
  if (mkdir(config_dir, 0700) != 0 || mkdir(app_dir, 0700) != 0) {
    goto cleanup;
  }
  FILE *file = fopen(path, "w");
  if (!file) {
    goto cleanup;
  }
  bool written = fputs("window:\n  width: 912\n", file) >= 0;
  if (fclose(file) != 0 || !written) {
    goto cleanup;
  }

  /* Keep the test process's environment intact, including the real HOME. */
  pid_t child = fork();
  if (child == 0) {
    if (setenv("HOME", root, 1) != 0 ||
        (use_xdg ? setenv("XDG_CONFIG_HOME", root, 1)
                 : unsetenv("XDG_CONFIG_HOME")) != 0) {
      _exit(2);
    }
    MitrConfig *cfg = config_load_default_path();
    bool matches = cfg && cfg->window_width == 912;
    config_destroy(cfg);
    _exit(matches ? 0 : 1);
  }
  if (child > 0) {
    int status;
    found = waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == 0;
  }

cleanup:
  remove(path);
  rmdir(app_dir);
  rmdir(config_dir);
  rmdir(root);
  return found;
}

TEST(config_load_default_path_uses_mitr_xdg_directory) {
  ASSERT_TRUE(config_finds_mitr_path(true));
}

TEST(config_load_default_path_uses_mitr_home_directory) {
  ASSERT_TRUE(config_finds_mitr_path(false));
}

/* ========================================================================
 * Test Suite Registration
 * ======================================================================== */

void run_config_tests(void) {
  printf("\n=== Configuration Tests ===\n");

  /* Default configuration */
  RUN_TEST(config_create_default_returns_valid_config);
  RUN_TEST(config_create_default_has_default_font);
  RUN_TEST(config_create_default_has_16_ansi_colors);
  RUN_TEST(config_create_default_has_cursor_settings);
  RUN_TEST(config_create_default_has_terminal_settings);
  RUN_TEST(config_destroy_handles_null);

  /* Color parsing */
  RUN_TEST(parse_hex_color_with_hash);
  RUN_TEST(parse_hex_color_without_hash);
  RUN_TEST(parse_hex_color_lowercase);
  RUN_TEST(parse_hex_color_invalid_format);

  /* Cursor style parsing */
  RUN_TEST(parse_cursor_style_valid);
  RUN_TEST(parse_cursor_style_case_insensitive);
  RUN_TEST(parse_cursor_style_invalid);

  /* OSC 52 policy (no SDL) */
  RUN_TEST(parse_osc52_mode_valid);
  RUN_TEST(parse_osc52_mode_case_insensitive);
  RUN_TEST(parse_osc52_mode_invalid);
  RUN_TEST(osc52_should_apply_off_drops);
  RUN_TEST(osc52_should_apply_on_writes);
  RUN_TEST(osc52_should_apply_confirm_follows_user);
  RUN_TEST(osc52_format_confirm_message_includes_length_and_preview);
  RUN_TEST(osc52_format_confirm_message_truncates_preview);
  RUN_TEST(config_load_osc52_modes);

  /* Config file loading */
  RUN_TEST(config_load_nonexistent_file_returns_defaults);
  RUN_TEST(config_load_empty_file_returns_defaults);
  RUN_TEST(config_load_partial_config_fills_defaults);
  RUN_TEST(config_load_full_config);
  RUN_TEST(config_load_sample_matches_defaults);
  RUN_TEST(config_sample_documents_all_keys);
  RUN_TEST(config_warns_on_unknown_key);
  RUN_TEST(config_rejects_non_numeric_garbage);
  RUN_TEST(config_rejects_unsafe_shell_path);
  RUN_TEST(config_load_invalid_values_use_defaults);

  /* Path resolution */
  RUN_TEST(config_load_default_path_returns_config);
  RUN_TEST(config_load_default_path_uses_mitr_xdg_directory);
  RUN_TEST(config_load_default_path_uses_mitr_home_directory);
}
