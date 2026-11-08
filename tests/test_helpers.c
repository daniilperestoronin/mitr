/**
 * test_helpers.c - Shared helpers for test suites
 */

#include "test_helpers.h"
#include <string.h>

void process_ansi_string(AnsiParser *parser, const char *str,
                         TerminalBuffer *tb) {
  ansi_parser_feed(parser, str, strlen(str), tb);
}
