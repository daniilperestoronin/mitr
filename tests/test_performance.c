/**
 * test_performance.c - Performance tests for mitr
 */

#include "ansi_parser.h"
#include "common.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include <sys/time.h>
#include <time.h>

/* ========================================================================
 * Helper Functions
 * ======================================================================== */

/**
 * Get current time in microseconds
 */
static uint64_t get_time_us(void) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

/**
 * Fill buffer with test data
 */
static void fill_buffer_with_text(TerminalBuffer *tb, const char *text) {
  size_t len = strlen(text);
  for (size_t i = 0; i < len; i++) {
    terminal_buffer_put_char(tb, text[i]);
  }
}

/* ========================================================================
 * Performance Tests
 * ======================================================================== */

/**
 * Test: Terminal buffer creation performance
 */
TEST(test_perf_buffer_init) {
  uint64_t start = get_time_us();

  /* Create and destroy 1000 buffers */
  for (int i = 0; i < 1000; i++) {
    TerminalBuffer *tb = terminal_buffer_init(24, 80);
    ASSERT_NOT_NULL(tb);
    terminal_buffer_destroy(tb);
  }

  uint64_t end = get_time_us();
  uint64_t elapsed = end - start;
  double avg_us = (double)elapsed / 1000.0;

  printf("    Buffer init/destroy: %.2f us average (total: %.2f ms)\n", avg_us,
         (double)elapsed / 1000.0);

  /* Should be fast - less than 100us per buffer on average */
  ASSERT_TRUE(avg_us < 100.0);
}

/**
 * Test: Character writing performance
 */
TEST(test_perf_char_writing) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);

  uint64_t start = get_time_us();

  /* Write 10,000 characters */
  for (int i = 0; i < 10000; i++) {
    terminal_buffer_put_char(tb, 'A' + (i % 26));
  }

  uint64_t end = get_time_us();
  uint64_t elapsed = end - start;
  double chars_per_ms = 10000.0 / ((double)elapsed / 1000.0);

  printf("    Character writing: %.0f chars/ms (%.2f ms for 10k chars)\n",
         chars_per_ms, (double)elapsed / 1000.0);

  terminal_buffer_destroy(tb);

  /* Should be able to write at least 1k chars/ms (relaxed for DEBUG+ASan) */
  ASSERT_TRUE(chars_per_ms > 1000.0);
}

/**
 * Test: Scrolling performance
 */
TEST(test_perf_scrolling) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(tb);

  /* Fill buffer */
  fill_buffer_with_text(tb, "Line of text that fills the buffer completely. ");

  uint64_t start = get_time_us();

  /* Scroll 1000 times */
  for (int i = 0; i < 1000; i++) {
    terminal_buffer_scroll_up(tb);
  }

  uint64_t end = get_time_us();
  uint64_t elapsed = end - start;
  double avg_us = (double)elapsed / 1000.0;

  printf("    Scrolling: %.2f us per scroll (%.2f ms for 1000 scrolls)\n",
         avg_us, (double)elapsed / 1000.0);

  terminal_buffer_destroy(tb);

  /* Should be fast - less than 50us per scroll */
  ASSERT_TRUE(avg_us < 50.0);
}

/**
 * Test: ANSI parser performance
 */
TEST(test_perf_ansi_parsing) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(parser);
  ASSERT_NOT_NULL(tb);

  /* Prepare test data with ANSI sequences */
  const char *test_data = "\x1b[31mRed text\x1b[0m "
                          "\x1b[1;32mBold green\x1b[0m "
                          "\x1b[38;2;255;128;0mTrue color\x1b[0m "
                          "Normal text ";

  size_t data_len = strlen(test_data);

  uint64_t start = get_time_us();

  /* Parse 1000 times */
  for (int i = 0; i < 1000; i++) {
    ansi_parser_feed(parser, test_data, data_len, tb);
    terminal_buffer_clear(tb);
    ansi_parser_reset(parser);
  }

  uint64_t end = get_time_us();
  uint64_t elapsed = end - start;
  double avg_us = (double)elapsed / 1000.0;

  printf("    ANSI parsing: %.2f us per parse (%.2f ms for 1000 parses)\n",
         avg_us, (double)elapsed / 1000.0);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);

  /* Should be fast - less than 100us per parse */
  ASSERT_TRUE(avg_us < 100.0);
}

/**
 * Test: Large output performance (simulating 'cat large_file')
 */
TEST(test_perf_large_output) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  ASSERT_NOT_NULL(parser);
  ASSERT_NOT_NULL(tb);

  /* Generate 1MB of test data */
  size_t data_size = 1024 * 1024; /* 1MB */
  char *test_data = malloc(data_size);
  if (!test_data) {
    ansi_parser_destroy(parser);
    terminal_buffer_destroy(tb);
    ASSERT_NOT_NULL(test_data);
  }

  /* Fill with printable ASCII */
  for (size_t i = 0; i < data_size - 1; i++) {
    if (i % 80 == 79) {
      test_data[i] = '\n';
    } else {
      test_data[i] = 'A' + (i % 26);
    }
  }
  test_data[data_size - 1] = '\0';

  uint64_t start = get_time_us();

  /* Parse all data */
  ansi_parser_feed(parser, test_data, data_size - 1, tb);

  uint64_t end = get_time_us();
  uint64_t elapsed = end - start;
  double mb_per_sec = 1.0 / ((double)elapsed / 1000000.0);

  printf("    Large output (1MB): %.2f MB/s (%.2f ms total)\n", mb_per_sec,
         (double)elapsed / 1000.0);

  free(test_data);
  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);

  /* Should be able to process at least 1 MB/s (relaxed for DEBUG+ASan) */
  ASSERT_TRUE(mb_per_sec > 1.0);
}

/**
 * Test: Memory usage for large buffers
 */
TEST(test_perf_memory_usage) {
  /* Test different buffer sizes */
  size_t sizes[][2] = {
      {24, 80},   /* Standard terminal */
      {50, 132},  /* Large terminal */
      {100, 200}, /* Very large */
  };

  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    size_t rows = sizes[i][0];
    size_t cols = sizes[i][1];

    TerminalBuffer *tb = terminal_buffer_init(rows, cols);
    ASSERT_NOT_NULL(tb);

    /* Calculate expected memory usage */
    size_t cells_size = rows * cols * sizeof(TermCell);
    size_t tabs_size = cols * sizeof(bool);
    size_t expected_kb = (cells_size + tabs_size) / 1024;

    printf("    Buffer %zux%zu: ~%zu KB expected\n", rows, cols, expected_kb);

    terminal_buffer_destroy(tb);
  }
}

/* ========================================================================
 * Test Suite
 * ======================================================================== */

void run_performance_tests(void) {
  printf("\n=== Performance Tests ===\n\n");

  RUN_TEST(test_perf_buffer_init);
  RUN_TEST(test_perf_char_writing);
  RUN_TEST(test_perf_scrolling);
  RUN_TEST(test_perf_ansi_parsing);
  RUN_TEST(test_perf_large_output);
  RUN_TEST(test_perf_memory_usage);
}
