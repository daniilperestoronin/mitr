/**
 * font_resolver.h - Native system font lookup
 */

#ifndef MITR_FONT_RESOLVER_H
#define MITR_FONT_RESOLVER_H

#include "common.h"

#define FONT_RESOLVER_PATH_MAX 1024
#define FONT_RESOLVER_FAMILY_MAX 256
#define FONT_RESOLVER_STYLE_MAX 256

typedef struct {
  char path[FONT_RESOLVER_PATH_MAX];
  char family[FONT_RESOLVER_FAMILY_MAX];
  char style[FONT_RESOLVER_STYLE_MAX];
  long face_index;
} FontMatch;

typedef struct FontResolver FontResolver;

/** Build the platform fallback list for family, or the default monospace. */
FontResolver *font_resolver_init(const char *family);

/** Release the fallback list. Safe to call with nullptr. */
void font_resolver_destroy(FontResolver *resolver);

/** Return the primary face selected while building the fallback list. */
bool font_resolver_primary(const FontResolver *resolver, FontMatch *out_match);

/** Return the first precomputed fallback face containing codepoint. */
bool font_resolver_fallback(const FontResolver *resolver, uint32_t codepoint,
                            FontMatch *out_match);

#endif /* MITR_FONT_RESOLVER_H */
