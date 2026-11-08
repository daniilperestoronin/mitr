/**
 * test_app_search.c - Query and wrap helpers (no SDL)
 */

#include "app_search.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include <string.h>

static void put_str(TerminalBuffer *tb, const char *s) {
  while (*s != '\0') {
    terminal_buffer_put_char(tb, *s);
    s++;
  }
}

TEST(app_search_enter_exit_resets_query) {
  AppSearch s;

  app_search_init(&s);
  app_search_enter(&s, nullptr);
  ASSERT_TRUE(s.active);
  ASSERT_EQ(s.query_len, 0);
  ASSERT_STR_EQ(s.query, "");
  ASSERT_FALSE(s.have_hit);

  app_search_append(&s, nullptr, "ab");
  ASSERT_EQ(s.query_len, 2);
  app_search_enter(&s, nullptr);
  ASSERT_STR_EQ(s.query, "ab");

  app_search_exit(&s, nullptr);
  ASSERT_FALSE(s.active);
  ASSERT_EQ(s.query_len, 0);
  ASSERT_STR_EQ(s.query, "");
  app_search_destroy(&s);
}

TEST(app_search_append_strips_controls_and_caps_query) {
  AppSearch s;
  char too_long[APP_SEARCH_QUERY_MAX + 8];
  size_t i;

  app_search_init(&s);
  app_search_enter(&s, nullptr);
  app_search_append(&s, nullptr, "a\nb\tc");
  ASSERT_STR_EQ(s.query, "abc");
  ASSERT_EQ(s.query_len, 3);

  app_search_append(&s, nullptr, nullptr);
  ASSERT_EQ(s.query_len, 3);

  memset(too_long, 'x', sizeof(too_long) - 1);
  too_long[sizeof(too_long) - 1] = '\0';
  app_search_append(&s, nullptr, too_long);
  ASSERT_EQ(s.query_len, 3);
  ASSERT_STR_EQ(s.query, "abc");

  app_search_exit(&s, nullptr);
  app_search_enter(&s, nullptr);
  for (i = 0; i < APP_SEARCH_QUERY_MAX - 1; i++) {
    too_long[i] = 'a';
  }
  too_long[APP_SEARCH_QUERY_MAX - 1] = '\0';
  app_search_append(&s, nullptr, too_long);
  ASSERT_EQ(s.query_len, APP_SEARCH_QUERY_MAX - 1);
  app_search_append(&s, nullptr, "z");
  ASSERT_EQ(s.query_len, APP_SEARCH_QUERY_MAX - 1);
  app_search_destroy(&s);
}

TEST(app_search_backspace_drops_one_utf8_character) {
  AppSearch s;

  app_search_init(&s);
  app_search_enter(&s, nullptr);
  app_search_append(&s, nullptr, "a\xC3\xA9x");
  ASSERT_EQ(s.query_len, 4);
  app_search_backspace(&s, nullptr);
  ASSERT_STR_EQ(s.query, "a\xC3\xA9");
  ASSERT_EQ(s.query_len, 3);
  app_search_backspace(&s, nullptr);
  ASSERT_STR_EQ(s.query, "a");
  ASSERT_EQ(s.query_len, 1);
  app_search_backspace(&s, nullptr);
  ASSERT_STR_EQ(s.query, "");
  app_search_backspace(&s, nullptr);
  ASSERT_EQ(s.query_len, 0);
  app_search_destroy(&s);
}

TEST(app_search_next_start_forward_and_backward) {
  size_t row = 0;
  size_t col = 0;

  app_search_next_start(false, false, 0, 0, 0, 9, 80, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  app_search_next_start(false, true, 2, 5, 0, 9, 80, &row, &col);
  ASSERT_EQ(row, 2);
  ASSERT_EQ(col, 6);

  app_search_next_start(false, true, 2, 79, 0, 9, 80, &row, &col);
  ASSERT_EQ(row, 3);
  ASSERT_EQ(col, 0);

  app_search_next_start(false, true, 9, 79, 0, 9, 80, &row, &col);
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);

  app_search_next_start(true, false, 0, 0, 0, 9, 80, &row, &col);
  ASSERT_EQ(row, 9);
  ASSERT_EQ(col, 79);

  app_search_next_start(true, true, 2, 5, 0, 9, 80, &row, &col);
  ASSERT_EQ(row, 2);
  ASSERT_EQ(col, 4);

  app_search_next_start(true, true, 2, 0, 0, 9, 80, &row, &col);
  ASSERT_EQ(row, 1);
  ASSERT_EQ(col, 79);

  app_search_next_start(true, true, 0, 0, 0, 9, 80, &row, &col);
  ASSERT_EQ(row, 9);
  ASSERT_EQ(col, 79);

  app_search_next_start(true, true, 5, 1, 5, 8, 10, &row, &col);
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 0);

  app_search_next_start(true, true, 5, 0, 5, 8, 10, &row, &col);
  ASSERT_EQ(row, 8);
  ASSERT_EQ(col, 9);
}

TEST(app_search_wrap_start_skips_when_already_at_origin) {
  size_t row = 99;
  size_t col = 99;

  ASSERT_TRUE(app_search_wrap_start(false, 0, 10, 79, 3, 4, &row, &col));
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);
  ASSERT_FALSE(app_search_wrap_start(false, 0, 10, 79, 0, 0, &row, &col));

  ASSERT_TRUE(app_search_wrap_start(true, 0, 10, 79, 0, 0, &row, &col));
  ASSERT_EQ(row, 10);
  ASSERT_EQ(col, 79);
  ASSERT_FALSE(app_search_wrap_start(true, 0, 10, 79, 10, 79, &row, &col));

  ASSERT_TRUE(app_search_wrap_start(false, 5, 8, 79, 6, 0, &row, &col));
  ASSERT_EQ(row, 5);
  ASSERT_EQ(col, 0);
  ASSERT_FALSE(app_search_wrap_start(false, 5, 8, 79, 5, 0, &row, &col));
}

TEST(app_search_find_wraps_forward_and_backward) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AppSearch s;

  ASSERT_NOT_NULL(tb);
  app_search_init(&s);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "foo bar foo");

  app_search_enter(&s, nullptr);
  app_search_append(&s, nullptr, "foo");
  app_search_find(&s, tb, nullptr, false);
  ASSERT_TRUE(s.have_hit);
  ASSERT_EQ(s.hit_row, 0);
  ASSERT_EQ(s.hit_col, 0);

  app_search_find(&s, tb, nullptr, false);
  ASSERT_EQ(s.hit_col, 8);

  app_search_find(&s, tb, nullptr, false);
  ASSERT_TRUE(s.have_hit);
  ASSERT_EQ(s.hit_col, 0);

  app_search_find(&s, tb, nullptr, true);
  ASSERT_EQ(s.hit_col, 8);

  app_search_find(&s, tb, nullptr, true);
  ASSERT_EQ(s.hit_col, 0);

  app_search_find(&s, tb, nullptr, true);
  ASSERT_EQ(s.hit_col, 8);

  app_search_destroy(&s);
  terminal_buffer_destroy(tb);
}

TEST(app_search_find_empty_query_clears_hit) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AppSearch s;

  ASSERT_NOT_NULL(tb);
  app_search_init(&s);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "hello");

  app_search_enter(&s, nullptr);
  app_search_append(&s, nullptr, "hello");
  app_search_find(&s, tb, nullptr, false);
  ASSERT_TRUE(s.have_hit);

  app_search_backspace(&s, nullptr);
  app_search_backspace(&s, nullptr);
  app_search_backspace(&s, nullptr);
  app_search_backspace(&s, nullptr);
  app_search_backspace(&s, nullptr);
  app_search_find(&s, tb, nullptr, false);
  ASSERT_FALSE(s.have_hit);

  app_search_destroy(&s);
  terminal_buffer_destroy(tb);
}

TEST(app_search_find_skips_scrollback_on_alt_screen) {
  TerminalBuffer *tb = terminal_buffer_init(4, 20);
  AppSearch s;
  size_t used;

  ASSERT_NOT_NULL(tb);
  app_search_init(&s);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "needle");
  for (size_t i = 0; i < terminal_buffer_get_rows(tb); i++) {
    terminal_buffer_scroll_up(tb);
  }
  used = terminal_buffer_get_scrollback_used(tb);
  ASSERT_EQ(used, 4);
  ASSERT_EQ(terminal_buffer_enter_alt_screen(tb), MITR_OK);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "alive");

  app_search_enter(&s, nullptr);
  app_search_append(&s, nullptr, "needle");
  app_search_find(&s, tb, nullptr, false);
  ASSERT_FALSE(s.have_hit);

  app_search_exit(&s, nullptr);
  app_search_enter(&s, nullptr);
  app_search_append(&s, nullptr, "alive");
  app_search_find(&s, tb, nullptr, false);
  ASSERT_TRUE(s.have_hit);
  ASSERT_EQ(s.hit_row, used);
  ASSERT_EQ(s.hit_col, 0);

  app_search_destroy(&s);
  terminal_buffer_destroy(tb);
}

TEST(app_search_find_is_case_insensitive) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  AppSearch s;

  ASSERT_NOT_NULL(tb);
  app_search_init(&s);
  terminal_buffer_set_cursor(tb, 0, 0);
  put_str(tb, "Hello");

  app_search_enter(&s, nullptr);
  app_search_append(&s, nullptr, "hello");
  app_search_find(&s, tb, nullptr, false);
  ASSERT_TRUE(s.have_hit);
  ASSERT_EQ(s.hit_row, 0);
  ASSERT_EQ(s.hit_col, 0);

  app_search_exit(&s, nullptr);
  app_search_enter(&s, nullptr);
  app_search_append(&s, nullptr, "HELLO");
  app_search_find(&s, tb, nullptr, false);
  ASSERT_TRUE(s.have_hit);
  ASSERT_EQ(s.hit_row, 0);
  ASSERT_EQ(s.hit_col, 0);

  app_search_destroy(&s);
  terminal_buffer_destroy(tb);
}

void run_app_search_tests(void) {
  printf("\n" TERM_COLOR_YELLOW "=== App Search Tests ===" TERM_COLOR_RESET
         "\n");
  RUN_TEST(app_search_enter_exit_resets_query);
  RUN_TEST(app_search_append_strips_controls_and_caps_query);
  RUN_TEST(app_search_backspace_drops_one_utf8_character);
  RUN_TEST(app_search_next_start_forward_and_backward);
  RUN_TEST(app_search_wrap_start_skips_when_already_at_origin);
  RUN_TEST(app_search_find_wraps_forward_and_backward);
  RUN_TEST(app_search_find_empty_query_clears_hit);
  RUN_TEST(app_search_find_skips_scrollback_on_alt_screen);
  RUN_TEST(app_search_find_is_case_insensitive);
}
