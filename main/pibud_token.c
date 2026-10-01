// main/pibud_token.c — see pibud_token.h.
#include "pibud_token.h"

#include <stdbool.h>

static bool is_separator(char c)
{
    return c == '-' || c == '_' || c == ':' || c == ' ' || c == '\t';
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int pibud_token_normalize(const char *input, char *out, size_t out_size)
{
    if (input == NULL || out == NULL || out_size < PIBUD_TOKEN_LEN + 1) {
        return -1;
    }

    size_t written = 0;
    for (const char *p = input; *p != '\0'; p++) {
        if (is_separator(*p)) {
            continue;
        }
        if (hex_digit(*p) < 0 || written >= PIBUD_TOKEN_LEN) {
            out[0] = '\0';
            return -1;
        }
        out[written++] = (*p >= 'A' && *p <= 'F') ? (char)(*p - 'A' + 'a') : *p;
    }

    out[written] = '\0';
    return (written == PIBUD_TOKEN_LEN) ? (int)written : -1;
}
