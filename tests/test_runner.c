/**
 * test_runner.c - Main test runner for mitr
 */

#include "common.h"
#include "mitr_log.h"
#include "test_framework.h"
#include "test_xterm.h"
#include <string.h>

void run_terminal_buffer_tests(void);
void run_ansi_parser_tests(void);
void run_cursor_control_tests(void);
void run_pty_manager_tests(void);
void run_mouse_tests(void);
void run_config_tests(void);
void run_input_encoder_tests(void);
void run_app_search_tests(void);
void run_vt100_tests(void);
void run_vt102_tests(void);
void run_vt220_tests(void);
void run_vt320_tests(void);
void run_renderer_tests(void);
void run_font_manager_tests(void);
void run_font_resolver_tests(void);
void run_unicode_tests(void);
void run_performance_tests(void);

/* ========================================================================
 * Basic Sanity Tests
 * ======================================================================== */

TEST(common_macros_min_max) {
  ASSERT_EQ(mitr_min(5, 10), 5);
  ASSERT_EQ(mitr_max(5, 10), 10);
  ASSERT_EQ(mitr_min(-5, 10), -5);
  ASSERT_EQ(mitr_max(-5, 10), 10);
}

TEST(common_macros_clamp) {
  ASSERT_EQ(mitr_clamp(5, 0, 10), 5);
  ASSERT_EQ(mitr_clamp(-5, 0, 10), 0);
  ASSERT_EQ(mitr_clamp(15, 0, 10), 10);
  ASSERT_EQ(mitr_clamp(0, 0, 10), 0);
  ASSERT_EQ(mitr_clamp(10, 0, 10), 10);
}

TEST(common_rgb_macros) {
  uint32_t color = RGB(255, 128, 64);
  ASSERT_EQ(RGB_RED(color), 255);
  ASSERT_EQ(RGB_GREEN(color), 128);
  ASSERT_EQ(RGB_BLUE(color), 64);
}

TEST(common_default_colors) {
  ASSERT_EQ(COLOR_BLACK, RGB(0, 0, 0));
  ASSERT_EQ(COLOR_WHITE, RGB(229, 229, 229));
  ASSERT_NE(COLOR_RED, COLOR_BLUE);
}

TEST(common_memory_helpers) {
  void *ptr = checked_malloc(100);
  ASSERT_NOT_NULL(ptr);
  free(ptr);

  void *ptr2 = checked_calloc(10, 10);
  ASSERT_NOT_NULL(ptr2);

  unsigned char *bytes = (unsigned char *)ptr2;
  bool all_zero = true;
  for (int i = 0; i < 100; i++) {
    if (bytes[i] != 0) {
      all_zero = false;
      break;
    }
  }
  ASSERT_TRUE(all_zero);

  free(ptr2);
}

TEST(common_display_scale) {
  /* No HiDPI display required: scale is drawable_w / window_w. */
  ASSERT_EQ((int)(mitr_display_scale(800, 800) * 100.0f + 0.5f), 100);
  ASSERT_EQ((int)(mitr_display_scale(800, 1600) * 100.0f + 0.5f), 200);
  ASSERT_EQ((int)(mitr_display_scale(800, 1200) * 100.0f + 0.5f), 150);
  ASSERT_EQ((int)(mitr_display_scale(0, 1600) * 100.0f + 0.5f), 100);
  ASSERT_EQ((int)(mitr_display_scale(800, 0) * 100.0f + 0.5f), 100);
  ASSERT_EQ((int)(mitr_display_scale(-10, 800) * 100.0f + 0.5f), 100);
  ASSERT_EQ((int)(mitr_display_scale(800, 400) * 100.0f + 0.5f), 100);

  ASSERT_EQ(mitr_drawable_to_logical(20, 2.0f), 10);
  ASSERT_EQ(mitr_drawable_to_logical(20, 1.0f), 20);
  ASSERT_EQ(mitr_logical_to_drawable(10, 2.0f), 20);
  ASSERT_EQ(mitr_logical_to_drawable(2, 2.0f), 4);
  ASSERT_EQ(mitr_drawable_to_logical(mitr_logical_to_drawable(10, 2.0f), 2.0f),
            10);
}

TEST(common_size_overflow_helpers) {
  size_t out = 0;

  ASSERT_FALSE(size_mul_overflow(24, 80, &out));
  ASSERT_EQ(out, 1920);

  ASSERT_TRUE(size_mul_overflow(SIZE_MAX, 2, &out));
  ASSERT_TRUE(size_mul_overflow(SIZE_MAX / 2 + 1, 2, &out));

  ASSERT_FALSE(size_add_overflow(10, 20, &out));
  ASSERT_EQ(out, 30);
  ASSERT_TRUE(size_add_overflow(SIZE_MAX, 1, &out));

  ASSERT_NULL(checked_calloc(SIZE_MAX, 2));
  ASSERT_NULL(checked_calloc(0, 16));
  ASSERT_NULL(checked_calloc(16, 0));
  ASSERT_TRUE(mitr_grid_dims_valid(24, 80));
  ASSERT_FALSE(mitr_grid_dims_valid(0, 80));
  ASSERT_FALSE(mitr_grid_dims_valid(MITR_MAX_ROWS + 1, 80));
  ASSERT_TRUE(mitr_scrollback_capacity_valid(1000));
  ASSERT_FALSE(mitr_scrollback_capacity_valid(MITR_MAX_SCROLLBACK + 1));
}

TEST(framework_assertions_work) {
  ASSERT_TRUE(true);
  ASSERT_FALSE(false);
  ASSERT_EQ(42, 42);
  ASSERT_NE(42, 43);
  ASSERT_NULL(nullptr);
  ASSERT_NOT_NULL(&g_test_stats);
  ASSERT_STR_EQ("hello", "hello");
}

static void run_sanity_tests(void) {
  printf("\n" TERM_COLOR_YELLOW "=== Basic Sanity Tests ===" TERM_COLOR_RESET
         "\n");
  RUN_TEST(common_macros_min_max);
  RUN_TEST(common_macros_clamp);
  RUN_TEST(common_rgb_macros);
  RUN_TEST(common_default_colors);
  RUN_TEST(common_memory_helpers);
  RUN_TEST(common_display_scale);
  RUN_TEST(common_size_overflow_helpers);
  RUN_TEST(framework_assertions_work);
}

static void run_unit_and_integration_tests(void) {
  run_sanity_tests();
  run_terminal_buffer_tests();
  run_ansi_parser_tests();
  run_cursor_control_tests();
  run_pty_manager_tests();
  run_mouse_tests();
  run_config_tests();
  run_input_encoder_tests();
  run_app_search_tests();
  run_vt100_tests();
  run_vt102_tests();
  run_vt220_tests();
  run_vt320_tests();
  run_xterm_tests();
  run_renderer_tests();
  run_font_manager_tests();
  run_font_resolver_tests();
  run_unicode_tests();
}

int main(int argc, char **argv) {
  bool run_perf = false;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--perf") == 0) {
      run_perf = true;
    }
  }

  log_set_level(LOG_WARN);
  test_init();

  if (run_perf) {
    printf("\n" TERM_COLOR_YELLOW "=== Performance Tests ===" TERM_COLOR_RESET
           "\n");
    run_performance_tests();
    return test_summary();
  }

  run_unit_and_integration_tests();
  return test_summary();
}
