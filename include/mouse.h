/**
 * @file mouse.h
 * @brief Mouse Manager - handles mouse selection and reporting
 *
 * Responsibilities:
 * - Track mouse selection (start, end, active state)
 * - Determine if a cell is selected
 * - Extract selected text from terminal buffer
 * - Mouse reporting for applications (optional)
 */

#ifndef MITR_MOUSE_H
#define MITR_MOUSE_H

#include "common.h"
#include "terminal_buffer.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct MouseManager MouseManager;

typedef enum { MOUSE_CELL_STRICT = 0, MOUSE_CELL_CLAMP } MouseCellPolicy;

/** Map a window pixel to the text grid, optionally clamping active gestures. */
bool mouse_pixel_to_cell(int pixel_x, int pixel_y, int padding_x, int padding_y,
                         int cell_width, int cell_height, size_t rows,
                         size_t cols, MouseCellPolicy policy, size_t *row,
                         size_t *col);

/**
 * @brief Initialize mouse manager
 * @return New MouseManager instance or nullptr on error
 */
MouseManager *mouse_manager_init(void);

/**
 * @brief Destroy mouse manager
 * @param mm Mouse manager to destroy
 */
void mouse_manager_destroy(MouseManager *mm);

/**
 * @brief Start a new selection
 * @param mm Mouse manager
 * @param row Starting row
 * @param col Starting column
 */
void mouse_manager_start_selection(MouseManager *mm, size_t row, size_t col);

/**
 * @brief Update selection end position (while dragging)
 * @param mm Mouse manager
 * @param row Current row
 * @param col Current column
 */
void mouse_manager_update_selection(MouseManager *mm, size_t row, size_t col);

/**
 * @brief Finish selection (mouse button released)
 * @param mm Mouse manager
 */
void mouse_manager_finish_selection(MouseManager *mm);

/**
 * @brief Clear current selection
 * @param mm Mouse manager
 */
void mouse_manager_clear_selection(MouseManager *mm);

/** Cancel an in-progress host selection when the terminal loses focus. */
void mouse_manager_focus_lost(MouseManager *mm);

/**
 * @brief Set an inclusive selection range (double/triple click).
 *
 * Marks the selection in progress so a later drag can still update the
 * end cell. A one-cell range stays active after finish_selection.
 */
void mouse_manager_select_range(MouseManager *mm, size_t start_row,
                                size_t start_col, size_t end_row,
                                size_t end_col);

/**
 * @brief Select the word at a view cell. Empty/space clears the selection.
 */
void mouse_manager_select_word(MouseManager *mm, const TerminalBuffer *tb,
                               size_t row, size_t col);

/**
 * @brief Select the whole view row [0, cols).
 */
void mouse_manager_select_line(MouseManager *mm, const TerminalBuffer *tb,
                               size_t row);

/**
 * @brief Check if a cell is part of the current selection
 * @param mm Mouse manager
 * @param row Cell row
 * @param col Cell column
 * @return true if cell is selected
 */
bool mouse_manager_is_selected(const MouseManager *mm, size_t row, size_t col);

/**
 * @brief Check if a selection is currently in progress or active
 * @param mm Mouse manager
 * @return true if selecting
 */
bool mouse_manager_is_selecting(const MouseManager *mm);

/** @return true only while the host selection button is held */
bool mouse_manager_is_dragging(const MouseManager *mm);

/**
 * @brief Get selected text from terminal buffer
 * @param mm Mouse manager
 * @param tb Terminal buffer
 * @return Allocated string with selected text (caller must free) or nullptr
 */
char *mouse_manager_get_selected_text(const MouseManager *mm,
                                      const TerminalBuffer *tb);

/**
 * @brief Enable/disable mouse reporting to PTY
 * @param mm Mouse manager
 * @param enabled Whether to enable reporting
 */
void mouse_manager_set_reporting(MouseManager *mm, bool enabled);

/**
 * @brief Check if mouse reporting is enabled
 * @param mm Mouse manager
 * @return true if reporting is enabled
 */
bool mouse_manager_is_reporting_enabled(const MouseManager *mm);

#endif /* MITR_MOUSE_H */
