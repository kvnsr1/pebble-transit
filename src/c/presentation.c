#include "presentation.h"
#include <string.h>

TransitEta transit_eta(int32_t epoch, int32_t now) {
  int32_t seconds = epoch - now;
  if (epoch <= 0) { return (TransitEta){ETA_NONE, 0}; }
  if (seconds < -59) { return (TransitEta){ETA_LEFT, 0}; }
  if (seconds < 60) { return (TransitEta){ETA_NOW, 0}; }
  if (seconds < 100 * 60) { return (TransitEta){ETA_MINUTES, seconds / 60}; }
  return (TransitEta){ETA_CLOCK, 0};
}

uint8_t transit_palette(uint32_t rgb) {
  // Round each RGB channel to its closest hardware level (0, 85, 170, 255).
  unsigned r = (((rgb >> 16) & 255) + 42) / 85;
  unsigned g = (((rgb >> 8) & 255) + 42) / 85;
  unsigned b = ((rgb & 255) + 42) / 85;
  return 0xC0 | (r << 4) | (g << 2) | b;
}

static int luminance(uint8_t color) {
  // Linearized sRGB luminance at the four actual display channel levels.
  static const int linear[] = {0, 91, 402, 1000};
  return (linear[(color >> 4) & 3] * 2126 + linear[(color >> 2) & 3] * 7152 +
    linear[color & 3] * 722) / 10000;
}

uint8_t transit_ink(uint32_t background, uint32_t requested) {
  int light = luminance(transit_palette(background));
  uint8_t ink = transit_palette(requested);
  int foreground = luminance(ink);
  int high = light > foreground ? light : foreground;
  int low = light > foreground ? foreground : light;
  if ((high + 50) * 10 >= (low + 50) * 45) { return ink; }
  // Choose the stronger black/white contrast if the API ink fails 4.5:1.
  return (light + 50) * (light + 50) >= 1050 * 50 ? 0xC0 : 0xFF;
}

bool transit_is_stale(int32_t updated_at, int32_t now, bool cached, bool connected) {
  return cached || !connected || updated_at <= 0 || now - updated_at > 120 ||
    updated_at - now > 120;
}

void transit_copy_text(char *dest, size_t size, const char *source) {
  if (!size) { return; }
  size_t length = strlen(source);
  if (length >= size) {
    length = size - 1;
    // Never split a multi-byte UTF-8 code point at a fixed watch buffer boundary.
    while (length && ((unsigned char)source[length] & 0xC0) == 0x80) { length--; }
  }
  memcpy(dest, source, length);
  dest[length] = '\0';
}
