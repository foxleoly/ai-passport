// main/pibud_form.c — see pibud_form.h.
#include "pibud_form.h"

#include <string.h>

static int hex_value(char c)
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

// Decodes one value span. Returns the decoded length or -1 on malformed input
// or when the result would not fit.
static int decode_value(const char *src, size_t len, char *out, size_t out_size)
{
    size_t n = 0;

    for (size_t i = 0; i < len; i++) {
        char c = src[i];

        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2 >= len) {
                return -1; // truncated escape
            }
            int hi = hex_value(src[i + 1]);
            int lo = hex_value(src[i + 2]);
            if (hi < 0 || lo < 0) {
                return -1; // not a hex escape
            }
            int value = (hi << 4) | lo;
            if (value == 0) {
                return -1; // a NUL cannot round-trip through a C string
            }
            c = (char)value;
            i += 2;
        }

        if (n + 1 >= out_size) {
            return -1; // refuse to truncate a credential
        }
        out[n++] = c;
    }

    out[n] = '\0';
    return (int)n;
}

int pibud_form_get(const char *body, size_t len, const char *key, char *out, size_t out_size)
{
    if (body == NULL || key == NULL || out == NULL || out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    const size_t key_len = strlen(key);
    size_t pos = 0;

    while (pos <= len) {
        size_t amp = pos;
        while (amp < len && body[amp] != '&') {
            amp++;
        }
        size_t eq = pos;
        while (eq < amp && body[eq] != '=') {
            eq++;
        }

        // A leading '=' would mean an empty key, which nothing looks up.
        if (eq > pos && eq - pos == key_len && memcmp(body + pos, key, key_len) == 0) {
            // Split on the first '=', so a value may itself contain '='.
            const char *value = (eq < amp) ? body + eq + 1 : "";
            size_t value_len = (eq < amp) ? amp - (eq + 1) : 0;
            int decoded = decode_value(value, value_len, out, out_size);
            if (decoded < 0) {
                out[0] = '\0';
            }
            return decoded;
        }

        if (amp >= len) {
            break;
        }
        pos = amp + 1;
    }

    return -1;
}
