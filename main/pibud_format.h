#pragma once

#include <stddef.h>
#include <stdint.h>

/* Compact token counts for the status pages: raw below 1000, then K/M/B/T with
 * at most one decimal ("12.3K", "1.2M", "3.4B", "1.2T"). Output is always
 * NUL-terminated and never exceeds PIBUD_FORMAT_TOKENS_MAX bytes with the NUL. */
#define PIBUD_FORMAT_TOKENS_MAX 16

void pibud_format_tokens(char *out, size_t out_size, uint64_t tokens);

/* Integer percentage of total, 0 when total is 0. Truncates, so the token buckets
 * can add up to one percent short of the total. */
unsigned pibud_percent(uint64_t part, uint64_t total);

/* Formats a local HH:MM from a UTC epoch plus a timezone offset, both in
 * seconds. Writes "--:--" while no time has arrived. */
#define PIBUD_FORMAT_CLOCK_MAX 6

void pibud_format_clock(char *out, size_t out_size, int64_t epoch_seconds,
                        int32_t tz_offset_seconds);
