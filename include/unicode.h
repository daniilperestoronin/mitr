/**
 * unicode.h - Terminal-aware Unicode width and classification
 */

#ifndef MITR_UNICODE_H
#define MITR_UNICODE_H

#include "common.h"

/**
 * Return display width like wcwidth: -1 invalid, 0 combining, 1 narrow, 2 wide.
 */
int unicode_wcwidth(uint32_t cp);

/**
 * Return true if codepoint is a combining mark (display width 0).
 */
bool unicode_is_combining(uint32_t cp);

#endif /* MITR_UNICODE_H */
