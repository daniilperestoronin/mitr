/**
 * fuzz_ansi_parser.c - libFuzzer entry for ANSI/UTF-8 parser
 *
 * Build: make fuzz   (requires Clang with -fsanitize=fuzzer)
 */

#include "ansi_parser.h"
#include "terminal_buffer.h"
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  AnsiParser *parser = ansi_parser_init();
  TerminalBuffer *tb = terminal_buffer_init(24, 80);
  if (!parser || !tb) {
    ansi_parser_destroy(parser);
    terminal_buffer_destroy(tb);
    return 0;
  }

  ansi_parser_feed(parser, (const char *)data, size, tb);

  ansi_parser_destroy(parser);
  terminal_buffer_destroy(tb);
  return 0;
}
