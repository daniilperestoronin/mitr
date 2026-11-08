/**
 * @file test_mouse.c
 * @brief Unit tests for Mouse Manager
 */

#include "mouse.h"
#include "terminal_buffer.h"
#include "test_framework.h"
#include <limits.h>

TEST(test_mouse_pixel_to_cell_strict_accepts_only_grid) {
  size_t row = 99;
  size_t col = 99;

  ASSERT_TRUE(mouse_pixel_to_cell(5, 7, 5, 7, 10, 20, 2, 3,
                                  MOUSE_CELL_STRICT, &row, &col));
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);
  ASSERT_TRUE(mouse_pixel_to_cell(34, 46, 5, 7, 10, 20, 2, 3,
                                  MOUSE_CELL_STRICT, &row, &col));
  ASSERT_EQ(row, 1);
  ASSERT_EQ(col, 2);

  ASSERT_FALSE(mouse_pixel_to_cell(4, 7, 5, 7, 10, 20, 2, 3,
                                   MOUSE_CELL_STRICT, &row, &col));
  ASSERT_FALSE(mouse_pixel_to_cell(5, 6, 5, 7, 10, 20, 2, 3,
                                   MOUSE_CELL_STRICT, &row, &col));
  ASSERT_FALSE(mouse_pixel_to_cell(35, 7, 5, 7, 10, 20, 2, 3,
                                   MOUSE_CELL_STRICT, &row, &col));
  ASSERT_FALSE(mouse_pixel_to_cell(5, 47, 5, 7, 10, 20, 2, 3,
                                   MOUSE_CELL_STRICT, &row, &col));
}

TEST(test_mouse_pixel_to_cell_clamps_active_gestures) {
  size_t row = 99;
  size_t col = 99;

  ASSERT_TRUE(mouse_pixel_to_cell(INT_MIN, INT_MIN, 5, 7, 10, 20, 2, 3,
                                  MOUSE_CELL_CLAMP, &row, &col));
  ASSERT_EQ(row, 0);
  ASSERT_EQ(col, 0);
  ASSERT_TRUE(mouse_pixel_to_cell(INT_MAX, INT_MAX, 5, 7, 10, 20, 2, 3,
                                  MOUSE_CELL_CLAMP, &row, &col));
  ASSERT_EQ(row, 1);
  ASSERT_EQ(col, 2);
}

TEST(test_mouse_pixel_to_cell_rejects_invalid_geometry) {
  size_t row = 99;
  size_t col = 99;

  ASSERT_FALSE(mouse_pixel_to_cell(5, 7, 5, 7, 0, 20, 2, 3,
                                   MOUSE_CELL_STRICT, &row, &col));
  ASSERT_FALSE(mouse_pixel_to_cell(5, 7, 5, 7, 10, -1, 2, 3,
                                   MOUSE_CELL_STRICT, &row, &col));
  ASSERT_FALSE(mouse_pixel_to_cell(5, 7, 5, 7, 10, 20, 0, 3,
                                   MOUSE_CELL_STRICT, &row, &col));
  ASSERT_FALSE(mouse_pixel_to_cell(5, 7, 5, 7, 10, 20, 2, 0,
                                   MOUSE_CELL_STRICT, &row, &col));
  ASSERT_FALSE(mouse_pixel_to_cell(5, 7, -1, 7, 10, 20, 2, 3,
                                   MOUSE_CELL_STRICT, &row, &col));
  ASSERT_FALSE(mouse_pixel_to_cell(5, 7, 5, 7, 10, 20, 2, 3,
                                   MOUSE_CELL_STRICT, nullptr, &col));
}

TEST(test_mouse_manager_init) {
  MouseManager *mm = mouse_manager_init();
  ASSERT_NOT_NULL(mm);
  ASSERT_FALSE(mouse_manager_is_selecting(mm));
  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_start_selection) {
  MouseManager *mm = mouse_manager_init();
  mouse_manager_start_selection(mm, 5, 10);

  ASSERT_TRUE(mouse_manager_is_selecting(mm));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 5, 10));

  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_update_selection) {
  MouseManager *mm = mouse_manager_init();
  mouse_manager_start_selection(mm, 5, 10);
  mouse_manager_update_selection(mm, 7, 20);

  ASSERT_TRUE(mouse_manager_is_selecting(mm));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 7, 20));

  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_finish_selection) {
  MouseManager *mm = mouse_manager_init();
  mouse_manager_start_selection(mm, 5, 10);
  mouse_manager_update_selection(mm, 7, 20);
  mouse_manager_finish_selection(mm);

  ASSERT_TRUE(mouse_manager_is_selecting(mm));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 6, 0));

  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_dragging_ends_when_selection_finishes) {
  MouseManager *mm = mouse_manager_init();

  ASSERT_FALSE(mouse_manager_is_dragging(mm));
  mouse_manager_start_selection(mm, 5, 10);
  ASSERT_TRUE(mouse_manager_is_dragging(mm));
  mouse_manager_update_selection(mm, 7, 20);
  ASSERT_TRUE(mouse_manager_is_dragging(mm));
  mouse_manager_finish_selection(mm);
  ASSERT_FALSE(mouse_manager_is_dragging(mm));
  ASSERT_TRUE(mouse_manager_is_selecting(mm));

  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_clear_selection) {
  MouseManager *mm = mouse_manager_init();
  mouse_manager_start_selection(mm, 5, 10);
  mouse_manager_update_selection(mm, 7, 20);
  mouse_manager_finish_selection(mm);
  mouse_manager_clear_selection(mm);

  ASSERT_FALSE(mouse_manager_is_selecting(mm));

  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_focus_loss_cancels_host_drag) {
  MouseManager *mm = mouse_manager_init();
  ASSERT_NOT_NULL(mm);

  mouse_manager_start_selection(mm, 5, 10);
  mouse_manager_update_selection(mm, 7, 20);
  ASSERT_TRUE(mouse_manager_is_dragging(mm));

  mouse_manager_focus_lost(mm);

  ASSERT_FALSE(mouse_manager_is_dragging(mm));
  ASSERT_FALSE(mouse_manager_is_selecting(mm));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 6, 0));
  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_focus_loss_preserves_finished_selection) {
  MouseManager *mm = mouse_manager_init();
  ASSERT_NOT_NULL(mm);

  mouse_manager_start_selection(mm, 5, 10);
  mouse_manager_update_selection(mm, 7, 20);
  mouse_manager_finish_selection(mm);
  ASSERT_TRUE(mouse_manager_is_selecting(mm));

  mouse_manager_focus_lost(mm);

  ASSERT_FALSE(mouse_manager_is_dragging(mm));
  ASSERT_TRUE(mouse_manager_is_selecting(mm));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 6, 0));
  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_is_selected_single_line) {
  MouseManager *mm = mouse_manager_init();
  mouse_manager_start_selection(mm, 5, 10);
  mouse_manager_update_selection(mm, 5, 20);

  /* Within selection */
  ASSERT_TRUE(mouse_manager_is_selected(mm, 5, 10));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 5, 15));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 5, 20));

  /* Outside selection */
  ASSERT_FALSE(mouse_manager_is_selected(mm, 5, 9));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 5, 21));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 4, 15));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 6, 15));

  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_is_selected_multi_line) {
  MouseManager *mm = mouse_manager_init();
  mouse_manager_start_selection(mm, 5, 10);
  mouse_manager_update_selection(mm, 7, 20);

  /* First line */
  ASSERT_TRUE(mouse_manager_is_selected(mm, 5, 10));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 5, 50));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 5, 9));

  /* Middle line */
  ASSERT_TRUE(mouse_manager_is_selected(mm, 6, 0));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 6, 50));

  /* Last line */
  ASSERT_TRUE(mouse_manager_is_selected(mm, 7, 0));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 7, 20));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 7, 21));

  /* Outside */
  ASSERT_FALSE(mouse_manager_is_selected(mm, 4, 15));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 8, 15));

  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_is_selected_reverse) {
  MouseManager *mm = mouse_manager_init();
  /* Select backwards (end before start) */
  mouse_manager_start_selection(mm, 7, 20);
  mouse_manager_update_selection(mm, 5, 10);

  /* Should still work correctly after normalization */
  ASSERT_TRUE(mouse_manager_is_selected(mm, 5, 10));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 6, 15));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 7, 20));

  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_get_selected_text_simple) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  terminal_buffer_put_char(tb, 'H');
  terminal_buffer_put_char(tb, 'e');
  terminal_buffer_put_char(tb, 'l');
  terminal_buffer_put_char(tb, 'l');
  terminal_buffer_put_char(tb, 'o');

  MouseManager *mm = mouse_manager_init();
  mouse_manager_start_selection(mm, 0, 0);
  mouse_manager_update_selection(mm, 0, 4);

  char *text = mouse_manager_get_selected_text(mm, tb);
  ASSERT_NOT_NULL(text);
  ASSERT_TRUE(strcmp(text, "Hello") == 0);

  free(text);
  mouse_manager_destroy(mm);
  terminal_buffer_destroy(tb);
}

TEST(test_mouse_manager_get_selected_text_no_selection) {
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  MouseManager *mm = mouse_manager_init();

  char *text = mouse_manager_get_selected_text(mm, tb);
  ASSERT_NOT_NULL(text);
  ASSERT_TRUE(strcmp(text, "") == 0);

  free(text);
  mouse_manager_destroy(mm);
  terminal_buffer_destroy(tb);
}

TEST(test_mouse_manager_reporting_disabled_by_default) {
  MouseManager *mm = mouse_manager_init();
  ASSERT_FALSE(mouse_manager_is_reporting_enabled(mm));
  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_reporting_can_be_enabled) {
  MouseManager *mm = mouse_manager_init();
  mouse_manager_set_reporting(mm, true);
  ASSERT_TRUE(mouse_manager_is_reporting_enabled(mm));
  mouse_manager_destroy(mm);
}

TEST(test_mouse_manager_select_word_around_spaces) {
  TerminalBuffer *tb = terminal_buffer_init(4, 20);
  MouseManager *mm = mouse_manager_init();
  char *text;

  ASSERT_NOT_NULL(tb);
  ASSERT_NOT_NULL(mm);
  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_char(tb, 'h');
  terminal_buffer_put_char(tb, 'e');
  terminal_buffer_put_char(tb, 'l');
  terminal_buffer_put_char(tb, 'l');
  terminal_buffer_put_char(tb, 'o');
  terminal_buffer_put_char(tb, ' ');
  terminal_buffer_put_char(tb, 'w');
  terminal_buffer_put_char(tb, 'o');
  terminal_buffer_put_char(tb, 'r');
  terminal_buffer_put_char(tb, 'l');
  terminal_buffer_put_char(tb, 'd');

  mouse_manager_select_word(mm, tb, 0, 1);
  mouse_manager_finish_selection(mm);
  ASSERT_TRUE(mouse_manager_is_selecting(mm));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 0, 0));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 0, 4));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 0, 5));
  text = mouse_manager_get_selected_text(mm, tb);
  ASSERT_NOT_NULL(text);
  ASSERT_STR_EQ(text, "hello");
  free(text);

  mouse_manager_select_word(mm, tb, 0, 9);
  mouse_manager_finish_selection(mm);
  text = mouse_manager_get_selected_text(mm, tb);
  ASSERT_NOT_NULL(text);
  ASSERT_STR_EQ(text, "world");
  free(text);

  mouse_manager_destroy(mm);
  terminal_buffer_destroy(tb);
}

TEST(test_mouse_manager_select_word_empty_cell) {
  TerminalBuffer *tb = terminal_buffer_init(4, 10);
  MouseManager *mm = mouse_manager_init();
  char *text;

  ASSERT_NOT_NULL(tb);
  ASSERT_NOT_NULL(mm);

  mouse_manager_select_word(mm, tb, 0, 3);
  mouse_manager_finish_selection(mm);
  ASSERT_FALSE(mouse_manager_is_selecting(mm));
  text = mouse_manager_get_selected_text(mm, tb);
  ASSERT_NOT_NULL(text);
  ASSERT_STR_EQ(text, "");
  free(text);

  mouse_manager_destroy(mm);
  terminal_buffer_destroy(tb);
}

TEST(test_mouse_manager_select_line_covers_whole_row) {
  TerminalBuffer *tb = terminal_buffer_init(3, 8);
  MouseManager *mm = mouse_manager_init();
  size_t cols;
  char *text;

  ASSERT_NOT_NULL(tb);
  ASSERT_NOT_NULL(mm);
  cols = terminal_buffer_get_cols(tb);
  terminal_buffer_set_cursor(tb, 1, 0);
  terminal_buffer_put_char(tb, 'A');
  terminal_buffer_put_char(tb, 'B');
  terminal_buffer_put_char(tb, 'C');

  mouse_manager_select_line(mm, tb, 1);
  mouse_manager_finish_selection(mm);
  ASSERT_TRUE(mouse_manager_is_selecting(mm));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 1, 0));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 1, cols - 1));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 0, 0));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 2, 0));

  text = mouse_manager_get_selected_text(mm, tb);
  ASSERT_NOT_NULL(text);
  ASSERT_STR_EQ(text, "ABC");
  free(text);

  mouse_manager_destroy(mm);
  terminal_buffer_destroy(tb);
}

TEST(test_mouse_manager_select_word_skips_continuation) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  MouseManager *mm = mouse_manager_init();
  char *text;

  ASSERT_NOT_NULL(tb);
  ASSERT_NOT_NULL(mm);
  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_codepoint(tb, 0x4E00); /* 一 */
  terminal_buffer_put_char(tb, ' ');
  terminal_buffer_put_char(tb, 'X');

  mouse_manager_select_word(mm, tb, 0, 1);
  mouse_manager_finish_selection(mm);
  ASSERT_TRUE(mouse_manager_is_selected(mm, 0, 0));
  ASSERT_TRUE(mouse_manager_is_selected(mm, 0, 1));
  ASSERT_FALSE(mouse_manager_is_selected(mm, 0, 2));

  text = mouse_manager_get_selected_text(mm, tb);
  ASSERT_NOT_NULL(text);
  ASSERT_EQ((int)strlen(text), 3);
  ASSERT_EQ((unsigned char)text[0], 0xE4u);
  ASSERT_EQ((unsigned char)text[1], 0xB8u);
  ASSERT_EQ((unsigned char)text[2], 0x80u);
  free(text);

  mouse_manager_destroy(mm);
  terminal_buffer_destroy(tb);
}

TEST(test_mouse_manager_select_word_single_cell_survives_finish) {
  TerminalBuffer *tb = terminal_buffer_init(3, 10);
  MouseManager *mm = mouse_manager_init();
  char *text;

  ASSERT_NOT_NULL(tb);
  ASSERT_NOT_NULL(mm);
  terminal_buffer_set_cursor(tb, 0, 0);
  terminal_buffer_put_char(tb, 'Q');

  mouse_manager_select_word(mm, tb, 0, 0);
  mouse_manager_finish_selection(mm);
  ASSERT_TRUE(mouse_manager_is_selecting(mm));
  text = mouse_manager_get_selected_text(mm, tb);
  ASSERT_NOT_NULL(text);
  ASSERT_STR_EQ(text, "Q");
  free(text);

  mouse_manager_destroy(mm);
  terminal_buffer_destroy(tb);
}

void run_mouse_tests(void) {
  printf("\n" TERM_COLOR_YELLOW "=== Mouse Manager Tests ===" TERM_COLOR_RESET
         "\n");
  RUN_TEST(test_mouse_pixel_to_cell_strict_accepts_only_grid);
  RUN_TEST(test_mouse_pixel_to_cell_clamps_active_gestures);
  RUN_TEST(test_mouse_pixel_to_cell_rejects_invalid_geometry);
  RUN_TEST(test_mouse_manager_init);
  RUN_TEST(test_mouse_manager_start_selection);
  RUN_TEST(test_mouse_manager_update_selection);
  RUN_TEST(test_mouse_manager_finish_selection);
  RUN_TEST(test_mouse_manager_dragging_ends_when_selection_finishes);
  RUN_TEST(test_mouse_manager_clear_selection);
  RUN_TEST(test_mouse_manager_focus_loss_cancels_host_drag);
  RUN_TEST(test_mouse_manager_focus_loss_preserves_finished_selection);
  RUN_TEST(test_mouse_manager_is_selected_single_line);
  RUN_TEST(test_mouse_manager_is_selected_multi_line);
  RUN_TEST(test_mouse_manager_is_selected_reverse);
  RUN_TEST(test_mouse_manager_get_selected_text_simple);
  RUN_TEST(test_mouse_manager_get_selected_text_no_selection);
  RUN_TEST(test_mouse_manager_reporting_disabled_by_default);
  RUN_TEST(test_mouse_manager_reporting_can_be_enabled);
  RUN_TEST(test_mouse_manager_select_word_around_spaces);
  RUN_TEST(test_mouse_manager_select_word_empty_cell);
  RUN_TEST(test_mouse_manager_select_line_covers_whole_row);
  RUN_TEST(test_mouse_manager_select_word_skips_continuation);
  RUN_TEST(test_mouse_manager_select_word_single_cell_survives_finish);
}
