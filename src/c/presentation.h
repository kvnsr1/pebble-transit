#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Pure presentation rules are shared by the watch and the host-side tests.
typedef enum { ETA_NONE, ETA_LEFT, ETA_NOW, ETA_MINUTES, ETA_CLOCK } EtaKind;
typedef struct { EtaKind kind; int minutes; } TransitEta;

TransitEta transit_eta(int32_t epoch, int32_t now);
uint8_t transit_palette(uint32_t rgb);
uint8_t transit_ink(uint32_t background, uint32_t requested);
bool transit_is_stale(int32_t updated_at, int32_t now, bool cached, bool connected);
void transit_copy_text(char *dest, size_t size, const char *source);
