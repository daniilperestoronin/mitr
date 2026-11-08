/**
 * test_font_resolver.c - Native font resolver tests
 */

#include "font_resolver.h"
#include "test_framework.h"
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <fontconfig/fontconfig.h>
#include <fontconfig/fcfreetype.h>
#endif

static bool path_readable(const char *path) {
  FILE *file;

  if (!path || !path[0]) {
    return false;
  }
  file = fopen(path, "r");
  if (!file) {
    return false;
  }
  fclose(file);
  return true;
}

TEST(font_resolver_finds_default_monospace) {
  FontResolver *resolver = font_resolver_init(nullptr);
  FontMatch match = {0};

  ASSERT_NOT_NULL(resolver);
  ASSERT_TRUE(font_resolver_primary(resolver, &match));
  ASSERT_TRUE(path_readable(match.path));
  ASSERT_TRUE(match.family[0] != '\0');
  font_resolver_destroy(resolver);
}

TEST(font_resolver_builds_reusable_fallback_list) {
  FontResolver *resolver = font_resolver_init(nullptr);
  FontMatch first = {0};
  FontMatch second = {0};

  ASSERT_NOT_NULL(resolver);

#if defined(__APPLE__)
  if (font_resolver_fallback(resolver, 0x231A, &first)) {
    ASSERT_TRUE(path_readable(first.path));
    ASSERT_TRUE(strcmp(first.family, "Apple Color Emoji") != 0);
    ASSERT_TRUE(font_resolver_fallback(resolver, 0x231A, &second));
    ASSERT_STR_EQ(first.path, second.path);
  }
#else
  ASSERT_TRUE(font_resolver_fallback(resolver, 0x231A, &first));
  ASSERT_TRUE(path_readable(first.path));
  ASSERT_TRUE(font_resolver_fallback(resolver, 0x231A, &second));
  ASSERT_STR_EQ(first.path, second.path);
#endif
  font_resolver_destroy(resolver);
}

#if defined(__linux__)
static bool noto_color_emoji_installed(void) {
  FcPattern *pattern;
  FcPattern *matched;
  FcResult result;
  FcChar8 *family = nullptr;
  FcBool color = FcFalse;
  bool installed = false;

  pattern = FcPatternCreate();
  if (!pattern) {
    return false;
  }
  if (!FcPatternAddString(pattern, FC_FAMILY,
                          (const FcChar8 *)"Noto Color Emoji")) {
    FcPatternDestroy(pattern);
    return false;
  }
  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  matched = FcFontMatch(nullptr, pattern, &result);
  FcPatternDestroy(pattern);
  if (matched &&
      FcPatternGetString(matched, FC_FAMILY, 0, &family) == FcResultMatch &&
      FcPatternGetBool(matched, FC_COLOR, 0, &color) == FcResultMatch &&
      strcmp((const char *)family, "Noto Color Emoji") == 0 && color) {
    installed = true;
  }
  if (matched) {
    FcPatternDestroy(matched);
  }
  return installed;
}

TEST(font_resolver_avoids_color_emoji_faces) {
  FontResolver *resolver;
  FontMatch match = {0};
  FcPattern *pattern;
  FcBool color = FcFalse;
  int face_count = 0;

  if (!noto_color_emoji_installed()) {
    return;
  }
  resolver = font_resolver_init(nullptr);
  ASSERT_NOT_NULL(resolver);
  if (!font_resolver_fallback(resolver, 0x1F9D1, &match)) {
    font_resolver_destroy(resolver);
    return;
  }
  pattern =
      FcFreeTypeQuery((const FcChar8 *)match.path,
                      (unsigned int)match.face_index, nullptr, &face_count);
  ASSERT_NOT_NULL(pattern);
  if (FcPatternGetBool(pattern, FC_COLOR, 0, &color) == FcResultMatch) {
    ASSERT_FALSE(color);
  }
  FcPatternDestroy(pattern);
  font_resolver_destroy(resolver);
}
#endif

TEST(font_resolver_rejects_invalid_arguments) {
  FontResolver *resolver = font_resolver_init(nullptr);
  FontMatch match = {0};

  ASSERT_NOT_NULL(resolver);
  ASSERT_FALSE(font_resolver_primary(nullptr, &match));
  ASSERT_FALSE(font_resolver_primary(resolver, nullptr));
  ASSERT_FALSE(font_resolver_fallback(nullptr, 0x231A, &match));
  ASSERT_FALSE(font_resolver_fallback(resolver, 0x110000, &match));
  ASSERT_FALSE(font_resolver_fallback(resolver, 0x231A, nullptr));
  font_resolver_destroy(resolver);
  font_resolver_destroy(nullptr);
}

void run_font_resolver_tests(void) {
  printf("\n" TERM_COLOR_YELLOW "=== Font Resolver Tests ===" TERM_COLOR_RESET
         "\n");
  RUN_TEST(font_resolver_finds_default_monospace);
  RUN_TEST(font_resolver_builds_reusable_fallback_list);
#if defined(__linux__)
  RUN_TEST(font_resolver_avoids_color_emoji_faces);
#endif
  RUN_TEST(font_resolver_rejects_invalid_arguments);
}
