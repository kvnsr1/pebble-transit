#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/c/presentation.h"
int main(void) {
  const int32_t now = 100000;
  assert(transit_eta(0, now).kind == ETA_NONE);
  assert(transit_eta(now - 60, now).kind == ETA_LEFT);
  assert(transit_eta(now - 59, now).kind == ETA_NOW);
  assert(transit_eta(now + 59, now).kind == ETA_NOW);
  assert(transit_eta(now + 60, now).minutes == 1);
  assert(transit_eta(now + 239, now).minutes == 3);
  assert(transit_eta(now + 5999, now).minutes == 99);
  assert(transit_eta(now + 6000, now).kind == ETA_CLOCK);
  assert(transit_palette(0x29A66A) == 0xC9);
  assert(transit_palette(0x2B2B2B) == 0xD5);
  assert(transit_ink(0xFFFF00, 0xFFFF00) == 0xC0);
  assert(transit_ink(0x000000, 0x000000) == 0xFF);
  assert(!transit_is_stale(now - 120, now, false, true));
  assert(transit_is_stale(now - 121, now, false, true));
  assert(transit_is_stale(now, now, true, true));
  assert(transit_is_stale(now, now, false, false));
  assert(transit_is_stale(now + 121, now, false, true));
  assert(transit_is_stale(0, now, false, true));
  char value[5];
  transit_copy_text(value, sizeof(value), "a\xE2\x80\xA6z");
  assert(strcmp(value, "a\xE2\x80\xA6") == 0);
  char tiny[3];
  transit_copy_text(tiny, sizeof(tiny), "a\xE2\x80\xA6z");
  assert(strcmp(tiny, "a") == 0);
  transit_copy_text(value, sizeof(value), "abc"); assert(strcmp(value, "abc") == 0);
  puts("Presentation boundary, palette, freshness, and UTF-8 tests passed");
}
