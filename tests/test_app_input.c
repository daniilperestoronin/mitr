/**
 * test_app_input.c - SDL shortcut normalization
 */

#include "app_input.h"
#include "test_framework.h"

TEST(app_input_ctrl_non_latin_letters) {
  const SDL_Keysym keys[] = {
      {.scancode = SDL_SCANCODE_B, .sym = 0x438, .mod = KMOD_LCTRL},
      {.scancode = SDL_SCANCODE_C, .sym = 0x441, .mod = KMOD_RCTRL},
      {.scancode = SDL_SCANCODE_V, .sym = 0x43c, .mod = KMOD_LCTRL},
  };
  const SDL_Keycode expected[] = {SDLK_b, SDLK_c, SDLK_v};
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    ASSERT_EQ(app_key_from_sdl(&keys[i]), expected[i]);
  }
}

TEST(app_input_gui_shift_non_latin_letter) {
  SDL_Keysym key = {
      .scancode = SDL_SCANCODE_F, .sym = 0x430, .mod = KMOD_LGUI | KMOD_LSHIFT};
  ASSERT_EQ(app_key_from_sdl(&key), SDLK_f);
}

TEST(app_input_preserves_latin_layout_mapping) {
  SDL_Keysym key = {
      .scancode = SDL_SCANCODE_B, .sym = SDLK_x, .mod = KMOD_LCTRL};
  ASSERT_EQ(app_key_from_sdl(&key), SDLK_x);
  key.sym = SDLK_SEMICOLON;
  ASSERT_EQ(app_key_from_sdl(&key), SDLK_SEMICOLON);
}

TEST(app_input_preserves_text_and_alt_mapping) {
  SDL_Keysym key = {.scancode = SDL_SCANCODE_B, .sym = 0x438};
  ASSERT_EQ(app_key_from_sdl(&key), 0x438);
  key.mod = KMOD_LSHIFT;
  ASSERT_EQ(app_key_from_sdl(&key), 0x438);
  key.mod = KMOD_RALT | KMOD_MODE;
  ASSERT_EQ(app_key_from_sdl(&key), 0x438);
  key.mod |= KMOD_LCTRL;
  ASSERT_EQ(app_key_from_sdl(&key), 0x438);
}

TEST(app_input_preserves_special_and_unknown_keys) {
  SDL_Keysym key = {
      .scancode = SDL_SCANCODE_LEFT, .sym = SDLK_LEFT, .mod = KMOD_LCTRL};
  ASSERT_EQ(app_key_from_sdl(&key), SDLK_LEFT);
  key.scancode = SDL_SCANCODE_UNKNOWN;
  key.sym = 0x438;
  ASSERT_EQ(app_key_from_sdl(&key), 0x438);
  key.scancode = SDL_SCANCODE_B;
  key.sym = SDLK_UNKNOWN;
  ASSERT_EQ(app_key_from_sdl(&key), SDLK_UNKNOWN);
  ASSERT_EQ(app_key_from_sdl(nullptr), SDLK_UNKNOWN);
}

TEST(app_input_routes_non_latin_search_shortcut) {
  ASSERT_EQ(SDL_InitSubSystem(SDL_INIT_EVENTS), 0);
  AppSearch search;
  app_search_init(&search);
  AppInputCtx ctx = {.search = &search};
  SDL_Event event = {0};
  event.type = SDL_KEYDOWN;
  event.key.keysym = (SDL_Keysym){.scancode = SDL_SCANCODE_F,
                                  .sym = 0x430,
                                  .mod = KMOD_LCTRL | KMOD_LSHIFT};
  int pushed = SDL_PushEvent(&event);
  app_input_handle_events(&ctx);
  bool active = search.active;
  app_search_destroy(&search);
  SDL_QuitSubSystem(SDL_INIT_EVENTS);
  ASSERT_EQ(pushed, 1);
  ASSERT_TRUE(active);
}

void run_app_input_tests(void);

void run_app_input_tests(void) {
  RUN_TEST(app_input_ctrl_non_latin_letters);
  RUN_TEST(app_input_gui_shift_non_latin_letter);
  RUN_TEST(app_input_preserves_latin_layout_mapping);
  RUN_TEST(app_input_preserves_text_and_alt_mapping);
  RUN_TEST(app_input_preserves_special_and_unknown_keys);
  RUN_TEST(app_input_routes_non_latin_search_shortcut);
}
