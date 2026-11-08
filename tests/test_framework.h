/**
 * test_framework.h - Minimalist testing framework for mitr
 *
 * Simple, lightweight testing framework following TDD principles.
 * No external dependencies, just pure C.
 */

#ifndef MITR_TEST_FRAMEWORK_H
#define MITR_TEST_FRAMEWORK_H

#include "common.h"
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ========================================================================
 * Test Statistics
 * ======================================================================== */

typedef struct {
  int tests_run;
  int tests_passed;
  int tests_failed;
  const char *current_test;
} TestStats;

extern TestStats g_test_stats;
extern bool g_test_current_failed;

static sigjmp_buf g_timeout_jmp;
static volatile sig_atomic_t g_in_timeout_test = 0;

/* ========================================================================
 * Color Output (ANSI codes for terminal)
 * ======================================================================== */

#define TERM_COLOR_RESET "\033[0m"
#define TERM_COLOR_RED "\033[31m"
#define TERM_COLOR_GREEN "\033[32m"
#define TERM_COLOR_YELLOW "\033[33m"
#define TERM_COLOR_CYAN "\033[36m"

/* ========================================================================
 * Internal helpers
 * ======================================================================== */

static inline void test_mark_failed(void) { g_test_current_failed = true; }

static inline void test_timeout_handler(int sig) {
  (void)sig;
  if (g_in_timeout_test) {
    test_mark_failed();
    siglongjmp(g_timeout_jmp, 1);
  }
}

static inline void test_run_body(const char *name, void (*fn)(void)) {
  g_test_stats.current_test = name;
  g_test_stats.tests_run++;
  g_test_current_failed = false;
  printf(TERM_COLOR_CYAN "[ RUN  ] " TERM_COLOR_RESET "test_%s\n", name);
  fn();
  if (!g_test_current_failed) {
    printf(TERM_COLOR_GREEN "[ PASS ] " TERM_COLOR_RESET "test_%s\n", name);
    g_test_stats.tests_passed++;
  }
}

static inline void test_run_with_timeout(const char *name, void (*fn)(void),
                                         unsigned seconds) {
  g_test_stats.current_test = name;
  g_test_stats.tests_run++;
  g_test_current_failed = false;
  printf(TERM_COLOR_CYAN "[ RUN  ] " TERM_COLOR_RESET "test_%s\n", name);

  struct sigaction sa;
  struct sigaction old_sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = test_timeout_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGALRM, &sa, &old_sa);

  g_in_timeout_test = 1;
  if (sigsetjmp(g_timeout_jmp, 1) == 0) {
    alarm(seconds);
    fn();
    alarm(0);
  } else {
    alarm(0);
    printf(TERM_COLOR_RED "[ TIMEOUT ] " TERM_COLOR_RESET
                          "test_%s exceeded %u seconds\n",
           name, seconds);
    g_test_stats.tests_failed++;
  }
  g_in_timeout_test = 0;
  sigaction(SIGALRM, &old_sa, nullptr);

  if (!g_test_current_failed) {
    printf(TERM_COLOR_GREEN "[ PASS ] " TERM_COLOR_RESET "test_%s\n", name);
    g_test_stats.tests_passed++;
  }
}

/* ========================================================================
 * Test Macros
 * ======================================================================== */

#define TEST(name)                                                             \
  static void test_##name(void);                                               \
  static void test_##name(void)

#define RUN_TEST(name) test_run_body(#name, test_##name)

#define RUN_TEST_TIMEOUT(name, seconds)                                        \
  test_run_with_timeout(#name, test_##name, (unsigned)(seconds))

/* ========================================================================
 * Assertion Macros
 * ======================================================================== */

#define TEST_FAIL(msg)                                                         \
  do {                                                                         \
    printf(TERM_COLOR_RED "[ FAIL ] " TERM_COLOR_RESET "%s:%d: %s\n",          \
           __FILE__, __LINE__, msg);                                           \
    g_test_stats.tests_failed++;                                               \
    test_mark_failed();                                                        \
    return;                                                                    \
  } while (0)

#define ASSERT_TRUE(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      printf(TERM_COLOR_RED "[ FAIL ] " TERM_COLOR_RESET                       \
                            "%s:%d: Expected true, got false: %s\n",           \
             __FILE__, __LINE__, #expr);                                       \
      g_test_stats.tests_failed++;                                             \
      test_mark_failed();                                                      \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_FALSE(expr)                                                     \
  do {                                                                         \
    if (expr) {                                                                \
      printf(TERM_COLOR_RED "[ FAIL ] " TERM_COLOR_RESET                       \
                            "%s:%d: Expected false, got true: %s\n",           \
             __FILE__, __LINE__, #expr);                                       \
      g_test_stats.tests_failed++;                                             \
      test_mark_failed();                                                      \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(actual, expected)                                            \
  do {                                                                         \
    long long a = (long long)(actual);                                         \
    long long e = (long long)(expected);                                       \
    if (a != e) {                                                              \
      printf(TERM_COLOR_RED "[ FAIL ] " TERM_COLOR_RESET                       \
                            "%s:%d: Expected %lld, got %lld: %s\n",            \
             __FILE__, __LINE__, e, a, #actual);                               \
      g_test_stats.tests_failed++;                                             \
      test_mark_failed();                                                      \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_NE(actual, expected)                                            \
  do {                                                                         \
    long long a = (long long)(actual);                                         \
    long long e = (long long)(expected);                                       \
    if (a == e) {                                                              \
      printf(TERM_COLOR_RED "[ FAIL ] " TERM_COLOR_RESET                       \
                            "%s:%d: Expected not equal to %lld: %s\n",         \
             __FILE__, __LINE__, e, #actual);                                  \
      g_test_stats.tests_failed++;                                             \
      test_mark_failed();                                                      \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_NULL(ptr)                                                       \
  do {                                                                         \
    if ((ptr) != nullptr) {                                                    \
      printf(TERM_COLOR_RED "[ FAIL ] " TERM_COLOR_RESET                       \
                            "%s:%d: Expected nullptr, got %p: %s\n",           \
             __FILE__, __LINE__, (void *)(ptr), #ptr);                         \
      g_test_stats.tests_failed++;                                             \
      test_mark_failed();                                                      \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_NOT_NULL(ptr)                                                   \
  do {                                                                         \
    if ((ptr) == nullptr) {                                                    \
      printf(TERM_COLOR_RED "[ FAIL ] " TERM_COLOR_RESET                       \
                            "%s:%d: Expected non-nullptr: %s\n",               \
             __FILE__, __LINE__, #ptr);                                        \
      g_test_stats.tests_failed++;                                             \
      test_mark_failed();                                                      \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_STR_EQ(actual, expected)                                        \
  do {                                                                         \
    const char *a = (actual);                                                  \
    const char *e = (expected);                                                \
    if (!a || !e || strcmp(a, e) != 0) {                                       \
      printf(TERM_COLOR_RED "[ FAIL ] " TERM_COLOR_RESET                       \
                            "%s:%d: Expected \"%s\", got \"%s\": %s\n",        \
             __FILE__, __LINE__, e ? e : "(null)", a ? a : "(null)", #actual); \
      g_test_stats.tests_failed++;                                             \
      test_mark_failed();                                                      \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_MEM_EQ(actual, expected, size)                                  \
  do {                                                                         \
    const void *a = (actual);                                                  \
    const void *e = (expected);                                                \
    size_t s = (size);                                                         \
    if (!a || !e || memcmp(a, e, s) != 0) {                                    \
      printf(TERM_COLOR_RED "[ FAIL ] " TERM_COLOR_RESET                       \
                            "%s:%d: Memory differs: %s\n",                     \
             __FILE__, __LINE__, #actual);                                     \
      g_test_stats.tests_failed++;                                             \
      test_mark_failed();                                                      \
      return;                                                                  \
    }                                                                          \
  } while (0)

/* ========================================================================
 * Test Suite Management
 * ======================================================================== */

static inline void test_init(void) {
  g_test_stats.tests_run = 0;
  g_test_stats.tests_passed = 0;
  g_test_stats.tests_failed = 0;
  g_test_stats.current_test = nullptr;
  g_test_current_failed = false;

  printf(TERM_COLOR_CYAN "========================================\n");
  printf("  mitr Test Suite\n");
  printf("========================================\n" TERM_COLOR_RESET);
}

static inline int test_summary(void) {
  printf("\n");
  printf(TERM_COLOR_CYAN "========================================\n");
  printf("  Test Results\n");
  printf("========================================\n" TERM_COLOR_RESET);
  printf("Tests run:    %d\n", g_test_stats.tests_run);
  printf(TERM_COLOR_GREEN "Tests passed: %d\n" TERM_COLOR_RESET,
         g_test_stats.tests_passed);

  if (g_test_stats.tests_failed > 0) {
    printf(TERM_COLOR_RED "Tests failed: %d\n" TERM_COLOR_RESET,
           g_test_stats.tests_failed);
    printf(TERM_COLOR_RED "\nFAILURE\n" TERM_COLOR_RESET);
    return EXIT_FAILURE;
  }

  printf(TERM_COLOR_GREEN "\nSUCCESS - All tests passed!\n" TERM_COLOR_RESET);
  return EXIT_SUCCESS;
}

#endif /* MITR_TEST_FRAMEWORK_H */
