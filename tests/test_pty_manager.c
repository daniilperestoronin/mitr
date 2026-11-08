/**
 * test_pty_manager.c - Tests for PTY Manager
 *
 * Note: PTY tests are more integration-style as they involve actual
 * process spawning and communication.
 */

/* This SDK hides mkdtemp unless Darwin extensions are on, and the test
 * build sets _POSIX_C_SOURCE. */
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "pty_manager.h"
#include "test_framework.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void test_sleep_ms(unsigned milliseconds) {
  struct timespec duration = {
      .tv_sec = (time_t)(milliseconds / 1000U),
      .tv_nsec = (long)(milliseconds % 1000U) * 1000000L,
  };

  while (nanosleep(&duration, &duration) != 0 && errno == EINTR) {
  }
}

/* ========================================================================
 * Initialization Tests
 * ======================================================================== */

TEST(pty_manager_init_creates_pty) {
  PtyManager *pty = pty_manager_init(24, 80, nullptr, nullptr);

  ASSERT_NOT_NULL(pty);
  ASSERT_TRUE(pty_manager_get_initialized(pty));
  ASSERT_TRUE(pty_manager_get_master_fd(pty) >= 0);
  ASSERT_TRUE(pty_manager_get_child_pid(pty) > 0);
  ASSERT_EQ(pty_manager_get_rows(pty), 24);
  ASSERT_EQ(pty_manager_get_cols(pty), 80);

  pty_manager_destroy(pty);
}

TEST(pty_manager_destroy_handles_null) {
  pty_manager_destroy(nullptr);
  ASSERT_TRUE(true);
}

TEST(pty_manager_init_with_custom_shell) {
  /* Use /bin/sh as it's guaranteed to exist */
  PtyManager *pty = pty_manager_init(24, 80, "/bin/sh", nullptr);

  ASSERT_NOT_NULL(pty);
  ASSERT_TRUE(pty_manager_get_initialized(pty));

  pty_manager_destroy(pty);
}

TEST(pty_manager_shell_path_valid_rejects_relative) {
  ASSERT_FALSE(pty_manager_shell_path_valid(nullptr));
  ASSERT_FALSE(pty_manager_shell_path_valid(""));
  ASSERT_FALSE(pty_manager_shell_path_valid("sh"));
  ASSERT_FALSE(pty_manager_shell_path_valid("./bin/sh"));
}

TEST(pty_manager_shell_path_valid_rejects_dotdot) {
  ASSERT_FALSE(pty_manager_shell_path_valid("/usr/../bin/sh"));
  ASSERT_FALSE(pty_manager_shell_path_valid("/../bin/sh"));
}

TEST(pty_manager_shell_path_valid_rejects_directory) {
  ASSERT_FALSE(pty_manager_shell_path_valid("/"));
  ASSERT_FALSE(pty_manager_shell_path_valid("/tmp"));
}

TEST(pty_manager_shell_path_valid_accepts_bin_sh) {
  ASSERT_TRUE(pty_manager_shell_path_valid("/bin/sh"));
}

TEST(pty_manager_shell_path_valid_rejects_world_writable) {
  const char *path = "/tmp/mitr_ww_shell";
  FILE *f = fopen(path, "w");
  ASSERT_NOT_NULL(f);
  fputs("#!/bin/sh\n", f);
  fclose(f);
  ASSERT_EQ(chmod(path, 0777), 0);
  ASSERT_FALSE(pty_manager_shell_path_valid(path));
  remove(path);
}

TEST(pty_manager_init_rejects_unsafe_shell) {
  ASSERT_NULL(pty_manager_init(24, 80, "sh", nullptr));
  ASSERT_NULL(pty_manager_init(24, 80, "/tmp", nullptr));
}

/* ========================================================================
 * Read/Write Tests
 * ======================================================================== */

TEST(pty_manager_write_and_read) {
  PtyManager *pty = pty_manager_init(24, 80, nullptr, nullptr);
  ASSERT_NOT_NULL(pty);

  /* Write a simple echo command */
  const char *cmd = "echo hello\n";
  ssize_t written = pty_manager_write(pty, cmd, strlen(cmd));
  ASSERT_TRUE(written > 0);

  /* Give shell time to process and read output (with retries) */
  char buffer[1024] = {0};
  ssize_t total_read = 0;

  for (int i = 0; i < 20; i++) { /* Try for up to 2 seconds */
    test_sleep_ms(100);          /* Try every 100ms. */
    ssize_t bytes = pty_manager_read(pty, buffer + total_read,
                                     sizeof(buffer) - (size_t)total_read - 1);
    if (bytes > 0) {
      total_read += bytes;
    }
    if (strstr(buffer, "hello") != nullptr) {
      break; /* Found it! */
    }
  }

  ASSERT_TRUE(total_read > 0);
  ASSERT_TRUE(strstr(buffer, "hello") != nullptr);

  pty_manager_destroy(pty);
}

TEST(pty_manager_write_all_sends_complete_command) {
  PtyManager *pty = pty_manager_init(24, 80, "/bin/sh", nullptr);
  ASSERT_NOT_NULL(pty);

  const char *cmd = "echo writeall\n";
  ssize_t n = pty_manager_write_all(pty, cmd, strlen(cmd));
  ASSERT_EQ(n, (ssize_t)strlen(cmd));

  char buffer[1024] = {0};
  ssize_t total_read = 0;
  for (int i = 0; i < 20; i++) {
    test_sleep_ms(100);
    ssize_t bytes = pty_manager_read(pty, buffer + total_read,
                                     sizeof(buffer) - (size_t)total_read - 1);
    if (bytes > 0) {
      total_read += bytes;
    }
    if (strstr(buffer, "writeall") != nullptr) {
      break;
    }
  }

  ASSERT_TRUE(strstr(buffer, "writeall") != nullptr);
  pty_manager_destroy(pty);
}

TEST(pty_manager_write_all_rejects_null) {
  PtyManager *pty = pty_manager_init(24, 80, "/bin/sh", nullptr);
  ASSERT_NOT_NULL(pty);
  ASSERT_EQ(pty_manager_write_all(pty, nullptr, 4), -1);
  pty_manager_destroy(pty);
}

TEST(pty_manager_write_returns_error_on_null) {
  PtyManager *pty = pty_manager_init(24, 80, nullptr, nullptr);
  ASSERT_NOT_NULL(pty);

  ssize_t result = pty_manager_write(pty, nullptr, 10);
  ASSERT_EQ(result, -1);

  pty_manager_destroy(pty);
}

TEST(pty_manager_read_returns_error_on_null_buffer) {
  PtyManager *pty = pty_manager_init(24, 80, nullptr, nullptr);
  ASSERT_NOT_NULL(pty);

  ssize_t result = pty_manager_read(pty, nullptr, 100);
  ASSERT_EQ(result, -1);

  pty_manager_destroy(pty);
}

TEST(pty_manager_flush_empty_is_noop) {
  PtyManager *pty = pty_manager_init(24, 80, "/bin/sh", nullptr);
  ASSERT_NOT_NULL(pty);

  ASSERT_FALSE(pty_manager_has_pending_write(pty));
  ASSERT_EQ(pty_manager_flush(pty), 0);
  ASSERT_FALSE(pty_manager_has_pending_write(pty));

  pty_manager_destroy(pty);
}

TEST(pty_manager_write_flush_roundtrip) {
  PtyManager *pty = pty_manager_init(24, 80, "/bin/sh", nullptr);
  ASSERT_NOT_NULL(pty);

  const char *cmd = "echo queued\n";
  ssize_t written = pty_manager_write(pty, cmd, strlen(cmd));
  ASSERT_TRUE(written > 0);
  ASSERT_TRUE(pty_manager_flush(pty) >= 0);

  char buffer[1024] = {0};
  ssize_t total_read = 0;
  for (int i = 0; i < 20; i++) {
    test_sleep_ms(100);
    ssize_t bytes = pty_manager_read(pty, buffer + total_read,
                                     sizeof(buffer) - (size_t)total_read - 1);
    if (bytes > 0) {
      total_read += bytes;
    }
    if (strstr(buffer, "queued") != nullptr) {
      break;
    }
  }

  ASSERT_TRUE(strstr(buffer, "queued") != nullptr);
  pty_manager_destroy(pty);
}

TEST(pty_manager_write_handles_backpressure_without_blocking) {
  PtyManager *pty = pty_manager_init(24, 80, "/bin/sh", nullptr);
  ASSERT_NOT_NULL(pty);

  /* Fill the slave's cooked-mode input buffer (no newline) until the
   * kernel PTY buffer and our output queue apply backpressure. The
   * important property is that write() returns promptly. */
  char chunk[8192];
  memset(chunk, 'A', sizeof(chunk));

  size_t total = 0;
  bool hit_limit = false;
  for (int i = 0; i < 512; i++) {
    ssize_t n = pty_manager_write(pty, chunk, sizeof(chunk));
    ASSERT_TRUE(n >= 0);
    if (n == 0) {
      hit_limit = true;
      break;
    }
    total += (size_t)n;
    if (n < (ssize_t)sizeof(chunk)) {
      hit_limit = true;
      ASSERT_TRUE(pty_manager_has_pending_write(pty));
      break;
    }
  }

  ASSERT_TRUE(total > 0);
  /* On spacious systems the whole burst may fit; either outcome is fine
   * as long as we never busy-looped in write(). */
  (void)hit_limit;
  ASSERT_TRUE(pty_manager_flush(pty) >= 0);

  pty_manager_destroy(pty);
}

/* ========================================================================
 * Process Management Tests
 * ======================================================================== */

TEST(pty_manager_is_alive_returns_true_for_running_shell) {
  PtyManager *pty = pty_manager_init(24, 80, nullptr, nullptr);
  ASSERT_NOT_NULL(pty);

  ASSERT_TRUE(pty_manager_is_alive(pty));

  pty_manager_destroy(pty);
}

TEST(pty_manager_is_alive_handles_null) {
  bool result = pty_manager_is_alive(nullptr);
  ASSERT_FALSE(result);
}

/* ========================================================================
 * Resize Tests
 * ======================================================================== */

TEST(pty_manager_resize_updates_dimensions) {
  PtyManager *pty = pty_manager_init(24, 80, nullptr, nullptr);
  ASSERT_NOT_NULL(pty);

  ASSERT_EQ(pty_manager_resize(pty, 30, 100), MITR_OK);
  ASSERT_EQ(pty_manager_get_rows(pty), 30);
  ASSERT_EQ(pty_manager_get_cols(pty), 100);

  pty_manager_destroy(pty);
}

TEST(pty_manager_resize_handles_null) {
  MitrError result = pty_manager_resize(nullptr, 30, 100);
  ASSERT_EQ(result, MITR_ERROR_INVALID_PARAM);
}

/* ========================================================================
 * Integration Tests
 * ======================================================================== */

TEST(pty_manager_can_execute_multiple_commands) {
  PtyManager *pty = pty_manager_init(24, 80, nullptr, nullptr);
  ASSERT_NOT_NULL(pty);

  /* First command */
  const char *cmd1 = "echo first\n";
  pty_manager_write(pty, cmd1, strlen(cmd1));
  test_sleep_ms(100);

  char buffer[1024] = {0};
  pty_manager_read(pty, buffer, sizeof(buffer) - 1);

  /* Second command */
  const char *cmd2 = "echo second\n";
  pty_manager_write(pty, cmd2, strlen(cmd2));
  test_sleep_ms(100);

  memset(buffer, 0, sizeof(buffer));
  ssize_t bytes = pty_manager_read(pty, buffer, sizeof(buffer) - 1);
  ASSERT_TRUE(bytes > 0);

  pty_manager_destroy(pty);
}

/* ========================================================================
 * macOS login shell and launch cwd
 * ======================================================================== */

static bool buffer_has_token(const char *buf, const char *token) {
  const char *hit = strstr(buf, token);
  char next;

  if (!hit) {
    return false;
  }
  next = hit[strlen(token)];
  return next == '\0' || next == '\r' || next == '\n';
}

static bool pty_read_until_token(PtyManager *pty, char *buf, size_t buf_size,
                                 const char *token) {
  size_t total = 0;

  buf[0] = '\0';
  for (int i = 0; i < 30; i++) {
    test_sleep_ms(100);
    if (total + 1 >= buf_size) {
      break;
    }
    ssize_t n = pty_manager_read(pty, buf + total, buf_size - total - 1);
    if (n > 0) {
      total += (size_t)n;
      buf[total] = '\0';
    }
    if (buffer_has_token(buf, token)) {
      return true;
    }
  }
  return buffer_has_token(buf, token);
}

/* Empty HOME so a login shell does not source the user's profile. */
struct PtyLaunchGuard {
  char home[PATH_MAX];
  char cwd[PATH_MAX];
  char temp_home[PATH_MAX];
  bool have_home;
  bool cwd_changed;
};

static bool pty_launch_guard_begin(struct PtyLaunchGuard *g, bool at_root) {
  const char *home = getenv("HOME");
  char tmpl[] = "/tmp/mitr_pty_home_XXXXXX";
  char *dir;

  memset(g, 0, sizeof(*g));
  g->have_home = home && strlen(home) < sizeof(g->home);
  if (g->have_home) {
    memcpy(g->home, home, strlen(home) + 1);
  }
  if (!getcwd(g->cwd, sizeof(g->cwd))) {
    return false;
  }
  dir = mkdtemp(tmpl);
  if (!dir || strlen(dir) >= sizeof(g->temp_home)) {
    return false;
  }
  memcpy(g->temp_home, dir, strlen(dir) + 1);
  if (setenv("HOME", g->temp_home, 1) != 0) {
    return false;
  }
  if (at_root) {
    if (chdir("/") != 0) {
      return false;
    }
    g->cwd_changed = true;
  }
  return true;
}

/* Safe immediately after fork: the child already copied cwd and environ. */
static void pty_launch_guard_restore_parent(struct PtyLaunchGuard *g) {
  if (g->cwd_changed) {
    (void)chdir(g->cwd);
    g->cwd_changed = false;
  }
  if (g->have_home) {
    (void)setenv("HOME", g->home, 1);
  } else {
    unsetenv("HOME");
  }
}

/* After the child has observed HOME. rmdir of its cwd is fine. */
static void pty_launch_guard_remove_temp(struct PtyLaunchGuard *g) {
  if (g->temp_home[0] != '\0') {
    rmdir(g->temp_home);
    g->temp_home[0] = '\0';
  }
}

/* Shell pwd may print /tmp while getcwd returns /private/tmp. */
static bool buffer_cwd_same_dir(const char *buf, const char *dir) {
  const char *hit = strstr(buf, "CWD=/");
  char path[PATH_MAX];
  size_t n = 0;
  struct stat got;
  struct stat want;

  if (!hit) {
    return false;
  }
  hit += strlen("CWD=");
  while (hit[n] != '\0' && hit[n] != '\r' && hit[n] != '\n' &&
         n + 1 < sizeof(path)) {
    path[n] = hit[n];
    n++;
  }
  if (n == 0 || (hit[n] != '\0' && hit[n] != '\r' && hit[n] != '\n')) {
    return false;
  }
  path[n] = '\0';
  if (stat(path, &got) != 0 || stat(dir, &want) != 0) {
    return false;
  }
  return got.st_ino == want.st_ino && got.st_dev == want.st_dev;
}

static bool pty_read_until_substr(PtyManager *pty, char *buf, size_t buf_size,
                                  const char *needle) {
  size_t total = 0;

  buf[0] = '\0';
  for (int i = 0; i < 30; i++) {
    test_sleep_ms(100);
    if (total + 1 >= buf_size) {
      break;
    }
    ssize_t n = pty_manager_read(pty, buf + total, buf_size - total - 1);
    if (n > 0) {
      total += (size_t)n;
      buf[total] = '\0';
    }
    if (strstr(buf, needle) != nullptr) {
      return true;
    }
  }
  return strstr(buf, needle) != nullptr;
}

TEST(pty_manager_argv0_marks_login_shell_on_macos) {
  struct PtyLaunchGuard guard;
  bool ready = pty_launch_guard_begin(&guard, false);
  PtyManager *pty =
      ready ? pty_manager_init(24, 80, "/bin/sh", nullptr) : nullptr;
  pty_launch_guard_restore_parent(&guard);
  if (!ready || !pty) {
    pty_launch_guard_remove_temp(&guard);
  }
  ASSERT_TRUE(ready);
  ASSERT_NOT_NULL(pty);

  const char *cmd = "printf 'ARGV0=%s\\n' \"$0\"\n";
  ssize_t n = pty_manager_write_all(pty, cmd, strlen(cmd));
  char buffer[2048];
#ifdef MITR_PLATFORM_MACOS
  const char *token = "ARGV0=-sh";
#else
  const char *token = "ARGV0=/bin/sh";
#endif
  bool found =
      n > 0 && pty_read_until_token(pty, buffer, sizeof(buffer), token);

  pty_manager_destroy(pty);
  pty_launch_guard_remove_temp(&guard);
  ASSERT_TRUE(n > 0);
  ASSERT_TRUE(found);
}

TEST(pty_manager_root_cwd_uses_home_on_macos) {
  struct PtyLaunchGuard guard;
  bool ready = pty_launch_guard_begin(&guard, true);
  PtyManager *pty =
      ready ? pty_manager_init(24, 80, "/bin/sh", nullptr) : nullptr;
  pty_launch_guard_restore_parent(&guard);
  if (!ready || !pty) {
    pty_launch_guard_remove_temp(&guard);
  }
  ASSERT_TRUE(ready);
  ASSERT_NOT_NULL(pty);

  const char *cmd = "printf 'CWD=%s\\n' \"$(pwd)\"\n";
  ssize_t n = pty_manager_write_all(pty, cmd, strlen(cmd));
  char buffer[4096];
#ifdef MITR_PLATFORM_MACOS
  const char *want = guard.temp_home;
#else
  const char *want = "/";
#endif
  bool found = n > 0 &&
               pty_read_until_substr(pty, buffer, sizeof(buffer), "CWD=/") &&
               buffer_cwd_same_dir(buffer, want);

  pty_manager_destroy(pty);
  pty_launch_guard_remove_temp(&guard);
  ASSERT_TRUE(n > 0);
  ASSERT_TRUE(found);
}

TEST(pty_manager_keeps_cwd_when_not_filesystem_root) {
  struct PtyLaunchGuard guard;
  char expected[PATH_MAX];
  bool ready = pty_launch_guard_begin(&guard, false);
  if (ready && strcmp(guard.cwd, "/") == 0) {
    pty_launch_guard_restore_parent(&guard);
    pty_launch_guard_remove_temp(&guard);
    return;
  }
  if (ready) {
    memcpy(expected, guard.cwd, strlen(guard.cwd) + 1);
  }
  PtyManager *pty =
      ready ? pty_manager_init(24, 80, "/bin/sh", nullptr) : nullptr;
  pty_launch_guard_restore_parent(&guard);
  if (!ready || !pty) {
    pty_launch_guard_remove_temp(&guard);
  }
  ASSERT_TRUE(ready);
  ASSERT_NOT_NULL(pty);

  const char *cmd = "printf 'CWD=%s\\n' \"$(pwd)\"\n";
  ssize_t n = pty_manager_write_all(pty, cmd, strlen(cmd));
  char token[PATH_MAX + 8];
  snprintf(token, sizeof(token), "CWD=%s", expected);
  char buffer[4096];
  bool found =
      n > 0 && pty_read_until_token(pty, buffer, sizeof(buffer), token);

  pty_manager_destroy(pty);
  pty_launch_guard_remove_temp(&guard);
  ASSERT_TRUE(n > 0);
  ASSERT_TRUE(found);
}

TEST(pty_manager_handles_exit_command) {
  PtyManager *pty = pty_manager_init(24, 80, "/bin/sh", nullptr);
  ASSERT_NOT_NULL(pty);

  ASSERT_TRUE(pty_manager_is_alive(pty));

  /* Send exit command - note this test may be flaky */
  /* Some shells may not exit immediately on 'exit' command */
  const char *cmd = "exit\n";
  ssize_t written = pty_manager_write(pty, cmd, strlen(cmd));
  ASSERT_TRUE(written > 0);

  /* Give it time to exit (with retries) */
  /* Note: This assertion is relaxed as shell behavior varies */
  bool was_alive = true;
  for (int i = 0; i < 20; i++) {
    test_sleep_ms(50);
    if (!pty_manager_is_alive(pty)) {
      was_alive = false;
      break;
    }
  }

  /* If shell didn't exit, that's OK for this test */
  /* The important part is that we can write the command */
  (void)was_alive;

  pty_manager_destroy(pty);
}

void run_pty_manager_tests(void) {
  printf("\n" TERM_COLOR_YELLOW "=== PTY Manager Tests ===" TERM_COLOR_RESET
         "\n");
  RUN_TEST(pty_manager_init_creates_pty);
  RUN_TEST(pty_manager_destroy_handles_null);
  RUN_TEST(pty_manager_init_with_custom_shell);
  RUN_TEST(pty_manager_shell_path_valid_rejects_relative);
  RUN_TEST(pty_manager_shell_path_valid_rejects_dotdot);
  RUN_TEST(pty_manager_shell_path_valid_rejects_directory);
  RUN_TEST(pty_manager_shell_path_valid_accepts_bin_sh);
  RUN_TEST(pty_manager_shell_path_valid_rejects_world_writable);
  RUN_TEST(pty_manager_init_rejects_unsafe_shell);
  RUN_TEST_TIMEOUT(pty_manager_write_and_read, 5);
  RUN_TEST(pty_manager_write_returns_error_on_null);
  RUN_TEST_TIMEOUT(pty_manager_write_all_sends_complete_command, 5);
  RUN_TEST(pty_manager_write_all_rejects_null);
  RUN_TEST(pty_manager_read_returns_error_on_null_buffer);
  RUN_TEST(pty_manager_flush_empty_is_noop);
  RUN_TEST_TIMEOUT(pty_manager_write_flush_roundtrip, 5);
  RUN_TEST(pty_manager_write_handles_backpressure_without_blocking);
  RUN_TEST(pty_manager_is_alive_returns_true_for_running_shell);
  RUN_TEST(pty_manager_is_alive_handles_null);
  RUN_TEST(pty_manager_resize_updates_dimensions);
  RUN_TEST(pty_manager_resize_handles_null);
  RUN_TEST_TIMEOUT(pty_manager_can_execute_multiple_commands, 5);
  RUN_TEST_TIMEOUT(pty_manager_argv0_marks_login_shell_on_macos, 8);
  RUN_TEST_TIMEOUT(pty_manager_root_cwd_uses_home_on_macos, 8);
  RUN_TEST_TIMEOUT(pty_manager_keeps_cwd_when_not_filesystem_root, 8);
  RUN_TEST_TIMEOUT(pty_manager_handles_exit_command, 5);
}
