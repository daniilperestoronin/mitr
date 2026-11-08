/**
 * test_helpers.h - Shared helpers for test suites
 */

#ifndef MITR_TEST_HELPERS_H
#define MITR_TEST_HELPERS_H

#include "ansi_parser.h"
#include "terminal_buffer.h"

void process_ansi_string(AnsiParser *parser, const char *str,
                         TerminalBuffer *tb);

#endif /* MITR_TEST_HELPERS_H */
