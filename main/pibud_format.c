// main/pibud_format.c — see pibud_format.h.
//
// Integer-only on purpose: the fractional digit is one division away, and the
// device's printf is newlib's, so no floating point is needed (or wanted) for a
// status readout.
#include "pibud_format.h"

#include <stdio.h>

void pibud_format_tokens(char *out, size_t out_size, uint64_t tokens)
{
    static const struct {
        uint64_t divisor;
        char suffix;
    } steps[] = {
        { 1000000000000ULL, 'T' },
        { 1000000000ULL,    'B' },
        { 1000000ULL,       'M' },
        { 1000ULL,          'K' },
    };

    if (out == NULL || out_size == 0) {
        return;
    }

    for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        if (tokens >= steps[i].divisor) {
            uint64_t whole = tokens / steps[i].divisor;
            uint64_t frac = (tokens % steps[i].divisor) / (steps[i].divisor / 10);
            if (frac == 0) {
                snprintf(out, out_size, "%llu%c",
                         (unsigned long long)whole, steps[i].suffix);
            } else {
                snprintf(out, out_size, "%llu.%llu%c",
                         (unsigned long long)whole, (unsigned long long)frac,
                         steps[i].suffix);
            }
            return;
        }
    }
    snprintf(out, out_size, "%llu", (unsigned long long)tokens);
}

unsigned pibud_percent(uint64_t part, uint64_t total)
{
    if (total == 0) {
        return 0;
    }
    return (unsigned)((part * 100) / total);
}

void pibud_format_clock(char *out, size_t out_size, int64_t epoch_seconds,
                        int32_t tz_offset_seconds)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    if (epoch_seconds <= 0) {
        snprintf(out, out_size, "--:--");
        return;
    }
    int64_t day = (epoch_seconds + tz_offset_seconds) % 86400;
    if (day < 0) {
        day += 86400; /* tz behind UTC can push the local time into yesterday */
    }
    snprintf(out, out_size, "%02d:%02d", (int)(day / 3600), (int)((day % 3600) / 60));
}
