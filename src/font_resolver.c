/**
 * font_resolver.c - Native system font lookup
 */

#include "font_resolver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <fontconfig/fontconfig.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#endif

typedef struct {
  FontMatch match;
#if defined(__linux__)
  FcCharSet *coverage;
#elif defined(__APPLE__)
  CFCharacterSetRef coverage;
#endif
} FontCandidate;

struct FontResolver {
  FontCandidate *candidates;
  size_t candidate_count;
  size_t candidate_capacity;
};

static bool codepoint_valid(uint32_t codepoint) {
  return codepoint <= 0x10FFFFu &&
         !(codepoint >= 0xD800u && codepoint <= 0xDFFFu);
}

static bool match_set(FontMatch *match, const char *path, const char *family,
                      const char *style, long face_index) {
  int path_written;
  int family_written = 0;
  int style_written = 0;

  if (!match || !path || !path[0]) {
    return false;
  }
  path_written = snprintf(match->path, sizeof(match->path), "%s", path);
  if (family && family[0]) {
    family_written =
        snprintf(match->family, sizeof(match->family), "%s", family);
  } else {
    match->family[0] = '\0';
  }
  if (style && style[0]) {
    style_written = snprintf(match->style, sizeof(match->style), "%s", style);
  } else {
    match->style[0] = '\0';
  }
  if (path_written < 0 || (size_t)path_written >= sizeof(match->path) ||
      family_written < 0 || (size_t)family_written >= sizeof(match->family) ||
      style_written < 0 || (size_t)style_written >= sizeof(match->style)) {
    memset(match, 0, sizeof(*match));
    return false;
  }
  match->face_index = face_index;
  return true;
}

static FontResolver *resolver_alloc(size_t candidate_capacity) {
  FontResolver *resolver;

  if (candidate_capacity == 0) {
    return nullptr;
  }
  resolver = checked_calloc(1, sizeof(FontResolver));
  if (!resolver) {
    return nullptr;
  }
  resolver->candidates =
      checked_calloc(candidate_capacity, sizeof(FontCandidate));
  if (!resolver->candidates) {
    free(resolver);
    return nullptr;
  }
  resolver->candidate_capacity = candidate_capacity;
  return resolver;
}

#if defined(__linux__)

static bool fontconfig_init(void) {
  static bool initialized = false;

  if (initialized) {
    return true;
  }

  if (!FcInit()) {
    return false;
  }
  if (atexit(FcFini) != 0) {
    FcFini();
    return false;
  }
  initialized = true;
  return true;
}

static bool fontconfig_match_from_pattern(FcPattern *pattern,
                                          FontMatch *out_match) {
  FcChar8 *path = nullptr;
  FcChar8 *family = nullptr;
  FcChar8 *style = nullptr;
  int face_index = 0;

  if (FcPatternGetString(pattern, FC_FILE, 0, &path) != FcResultMatch ||
      FcPatternGetString(pattern, FC_FAMILY, 0, &family) != FcResultMatch) {
    return false;
  }
  (void)FcPatternGetString(pattern, FC_STYLE, 0, &style);
  (void)FcPatternGetInteger(pattern, FC_INDEX, 0, &face_index);
  return match_set(out_match, (const char *)path, (const char *)family,
                   (const char *)style, (long)face_index);
}

static bool fontconfig_candidate_add(FontResolver *resolver, FcPattern *pattern,
                                     FcCharSet *combined_coverage) {
  FontCandidate *candidate;
  FcCharSet *coverage = nullptr;
  FcBool color = FcFalse;
  FcBool changed = FcFalse;

  if (resolver->candidate_count >= resolver->candidate_capacity ||
      FcPatternGetCharSet(pattern, FC_CHARSET, 0, &coverage) != FcResultMatch ||
      FcCharSetIsSubset(coverage, combined_coverage)) {
    return false;
  }
  if (FcPatternGetBool(pattern, FC_COLOR, 0, &color) == FcResultMatch &&
      color) {
    return false;
  }

  candidate = &resolver->candidates[resolver->candidate_count];
  if (!fontconfig_match_from_pattern(pattern, &candidate->match)) {
    return false;
  }
  candidate->coverage = FcCharSetCopy(coverage);
  if (!candidate->coverage ||
      !FcCharSetMerge(combined_coverage, coverage, &changed)) {
    if (candidate->coverage) {
      FcCharSetDestroy(candidate->coverage);
      candidate->coverage = nullptr;
    }
    return false;
  }
  resolver->candidate_count++;
  return true;
}

FontResolver *font_resolver_init(const char *family) {
  const char *requested_family = family && family[0] ? family : "monospace";
  FontResolver *resolver = nullptr;
  FcPattern *pattern = nullptr;
  FcFontSet *font_set = nullptr;
  FcCharSet *combined_coverage = nullptr;
  FcResult result;

  if (!fontconfig_init()) {
    return nullptr;
  }
  pattern = FcPatternCreate();
  if (!pattern ||
      !FcPatternAddString(pattern, FC_FAMILY,
                          (const FcChar8 *)requested_family) ||
      (strcmp(requested_family, "monospace") != 0 &&
       !FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)"monospace")) ||
      !FcPatternAddBool(pattern, FC_SCALABLE, FcTrue)) {
    goto cleanup;
  }
  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  font_set = FcFontSort(nullptr, pattern, FcFalse, nullptr, &result);
  if (!font_set || font_set->nfont <= 0) {
    goto cleanup;
  }

  resolver = resolver_alloc((size_t)font_set->nfont);
  combined_coverage = FcCharSetCreate();
  if (!resolver || !combined_coverage) {
    font_resolver_destroy(resolver);
    resolver = nullptr;
    goto cleanup;
  }
  for (int i = 0; i < font_set->nfont; i++) {
    (void)fontconfig_candidate_add(resolver, font_set->fonts[i],
                                   combined_coverage);
  }
  if (resolver->candidate_count == 0) {
    font_resolver_destroy(resolver);
    resolver = nullptr;
  }

cleanup:
  if (combined_coverage) {
    FcCharSetDestroy(combined_coverage);
  }
  if (font_set) {
    FcFontSetDestroy(font_set);
  }
  if (pattern) {
    FcPatternDestroy(pattern);
  }
  return resolver;
}

void font_resolver_destroy(FontResolver *resolver) {
  if (!resolver) {
    return;
  }
  for (size_t i = 0; i < resolver->candidate_count; i++) {
    if (resolver->candidates[i].coverage) {
      FcCharSetDestroy(resolver->candidates[i].coverage);
    }
  }
  SAFE_FREE(resolver->candidates);
  free(resolver);
}

static bool candidate_has_codepoint(const FontCandidate *candidate,
                                    uint32_t codepoint) {
  return FcCharSetHasChar(candidate->coverage, (FcChar32)codepoint);
}

#elif defined(__APPLE__)

static CTFontRef coretext_create_base_font(const char *family) {
  CTFontRef font = nullptr;

  if (family && family[0]) {
    CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, family,
                                                 kCFStringEncodingUTF8);
    if (name) {
      font = CTFontCreateWithName(name, 12.0, nullptr);
      if (font) {
        CFStringRef actual_family = CTFontCopyFamilyName(font);
        if (!actual_family ||
            CFStringCompare(name, actual_family, kCFCompareCaseInsensitive) !=
                kCFCompareEqualTo) {
          CFRelease(font);
          font = nullptr;
        }
        if (actual_family) {
          CFRelease(actual_family);
        }
      }
      CFRelease(name);
    }
  }
  if (!font) {
    font = CTFontCreateUIFontForLanguage(kCTFontUIFontUserFixedPitch, 12.0,
                                         nullptr);
  }
  return font;
}

static bool coretext_match_from_font(CTFontRef font, FontMatch *out_match) {
  CFTypeRef attribute;
  CFStringRef actual_family;
  CFStringRef actual_style;
  char family[FONT_RESOLVER_FAMILY_MAX];
  char style[FONT_RESOLVER_STYLE_MAX] = {0};
  bool found = false;

  if (!font || !out_match) {
    return false;
  }
  actual_family = CTFontCopyFamilyName(font);
  if (!actual_family ||
      !CFStringGetCString(actual_family, family, sizeof(family),
                          kCFStringEncodingUTF8)) {
    if (actual_family) {
      CFRelease(actual_family);
    }
    return false;
  }
  CFRelease(actual_family);

  actual_style = CTFontCopyName(font, kCTFontStyleNameKey);
  if (actual_style) {
    (void)CFStringGetCString(actual_style, style, sizeof(style),
                             kCFStringEncodingUTF8);
    CFRelease(actual_style);
  }

  attribute = CTFontCopyAttribute(font, kCTFontURLAttribute);
  if (attribute && CFGetTypeID(attribute) == CFURLGetTypeID()) {
    char path[FONT_RESOLVER_PATH_MAX];
    if (CFURLGetFileSystemRepresentation((CFURLRef)attribute, true,
                                         (UInt8 *)path, sizeof(path))) {
      /* CoreText does not expose TTC face indexes through this attribute. */
      found = match_set(out_match, path, family, style, -1);
    }
  }
  if (attribute) {
    CFRelease(attribute);
  }
  return found;
}

static bool coretext_candidate_add(FontResolver *resolver, CTFontRef font,
                                   CFMutableCharacterSetRef combined_coverage) {
  FontCandidate *candidate;
  CFCharacterSetRef coverage;

  if (!font || resolver->candidate_count >= resolver->candidate_capacity ||
      (CTFontGetSymbolicTraits(font) & kCTFontColorGlyphsTrait) != 0) {
    return false;
  }
  coverage = CTFontCopyCharacterSet(font);
  if (!coverage || CFCharacterSetIsSupersetOfSet(combined_coverage, coverage)) {
    if (coverage) {
      CFRelease(coverage);
    }
    return false;
  }

  candidate = &resolver->candidates[resolver->candidate_count];
  if (!coretext_match_from_font(font, &candidate->match)) {
    CFRelease(coverage);
    return false;
  }
  candidate->coverage = coverage;
  CFCharacterSetUnion(combined_coverage, coverage);
  resolver->candidate_count++;
  return true;
}

FontResolver *font_resolver_init(const char *family) {
  FontResolver *resolver = nullptr;
  CTFontRef base = coretext_create_base_font(family);
  CTFontRef symbols = nullptr;
  CFArrayRef languages = nullptr;
  CFArrayRef cascade = nullptr;
  CFMutableCharacterSetRef combined_coverage = nullptr;
  CFIndex cascade_count = 0;

  if (!base) {
    return nullptr;
  }
  languages = CFLocaleCopyPreferredLanguages();
  cascade = CTFontCopyDefaultCascadeListForLanguages(base, languages);
  if (cascade) {
    cascade_count = CFArrayGetCount(cascade);
  }
  resolver = resolver_alloc((size_t)cascade_count + 2);
  combined_coverage = CFCharacterSetCreateMutable(kCFAllocatorDefault);
  if (!resolver || !combined_coverage) {
    font_resolver_destroy(resolver);
    resolver = nullptr;
    goto cleanup;
  }

  (void)coretext_candidate_add(resolver, base, combined_coverage);
  for (CFIndex i = 0; i < cascade_count; i++) {
    CTFontDescriptorRef descriptor =
        (CTFontDescriptorRef)CFArrayGetValueAtIndex(cascade, i);
    CTFontRef font = CTFontCreateWithFontDescriptor(descriptor, 12.0, nullptr);
    if (font) {
      (void)coretext_candidate_add(resolver, font, combined_coverage);
      CFRelease(font);
    }
  }
  symbols = CTFontCreateWithName(CFSTR("Apple Symbols"), 12.0, nullptr);
  if (symbols) {
    (void)coretext_candidate_add(resolver, symbols, combined_coverage);
  }
  if (resolver->candidate_count == 0) {
    font_resolver_destroy(resolver);
    resolver = nullptr;
  }

cleanup:
  if (symbols) {
    CFRelease(symbols);
  }
  if (combined_coverage) {
    CFRelease(combined_coverage);
  }
  if (cascade) {
    CFRelease(cascade);
  }
  if (languages) {
    CFRelease(languages);
  }
  CFRelease(base);
  return resolver;
}

void font_resolver_destroy(FontResolver *resolver) {
  if (!resolver) {
    return;
  }
  for (size_t i = 0; i < resolver->candidate_count; i++) {
    if (resolver->candidates[i].coverage) {
      CFRelease(resolver->candidates[i].coverage);
    }
  }
  SAFE_FREE(resolver->candidates);
  free(resolver);
}

static bool candidate_has_codepoint(const FontCandidate *candidate,
                                    uint32_t codepoint) {
  return CFCharacterSetIsLongCharacterMember(candidate->coverage,
                                             (UTF32Char)codepoint);
}

#else

FontResolver *font_resolver_init(const char *family) {
  (void)family;
  return nullptr;
}

void font_resolver_destroy(FontResolver *resolver) { (void)resolver; }

static bool candidate_has_codepoint(const FontCandidate *candidate,
                                    uint32_t codepoint) {
  (void)candidate;
  (void)codepoint;
  return false;
}

#endif

bool font_resolver_primary(const FontResolver *resolver, FontMatch *out_match) {
  if (!resolver || !out_match || resolver->candidate_count == 0) {
    return false;
  }
  *out_match = resolver->candidates[0].match;
  return true;
}

bool font_resolver_fallback(const FontResolver *resolver, uint32_t codepoint,
                            FontMatch *out_match) {
  if (!resolver || !out_match || !codepoint_valid(codepoint)) {
    return false;
  }
  for (size_t i = 1; i < resolver->candidate_count; i++) {
    if (candidate_has_codepoint(&resolver->candidates[i], codepoint)) {
      *out_match = resolver->candidates[i].match;
      return true;
    }
  }
  return false;
}
