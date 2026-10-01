// main/pibud_prov_html.c — see pibud_prov_html.h.
#include "pibud_prov_html.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Worst case per character is "&quot;" at six bytes, and a Wi-Fi SSID is at
// most 32 characters, so this always holds a full escaped SSID.
#define PIBUD_ESC_MAX 256

static const char *replacement_for(char c)
{
    switch (c) {
    case '&':
        return "&amp;";
    case '<':
        return "&lt;";
    case '>':
        return "&gt;";
    case '"':
        return "&quot;";
    case '\'':
        return "&#39;";
    default:
        return NULL;
    }
}

int pibud_html_escape(const char *value, char *out, size_t out_size)
{
    if (value == NULL || out == NULL || out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    size_t written = 0;

    for (const char *p = value; *p != '\0'; p++) {
        const char *rep = replacement_for(*p);
        size_t need = (rep != NULL) ? strlen(rep) : 1;

        if (written + need + 1 > out_size) {
            out[0] = '\0';
            return -1;
        }
        if (rep != NULL) {
            memcpy(out + written, rep, need);
        } else {
            out[written] = *p;
        }
        written += need;
    }

    out[written] = '\0';
    return (int)written;
}

int pibud_prov_options_html(const char *const *ssids, size_t count,
                            char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return -1;
    }
    out[0] = '\0';

    // No networks found is a normal state, so NULL with count 0 is fine.
    if (ssids == NULL) {
        return (count == 0) ? 0 : -1;
    }

    size_t written = 0;

    for (size_t i = 0; i < count; i++) {
        const char *ssid = ssids[i];
        if (ssid == NULL || ssid[0] == '\0') {
            continue;
        }

        bool duplicate = false;
        for (size_t j = 0; j < i; j++) {
            if (ssids[j] != NULL && strcmp(ssids[j], ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        char escaped[PIBUD_ESC_MAX];
        if (pibud_html_escape(ssid, escaped, sizeof(escaped)) < 0) {
            continue; // cannot be a real SSID; leave it out of the list
        }

        int n = snprintf(out + written, out_size - written,
                         "<option value=\"%s\">%s</option>", escaped, escaped);
        if (n < 0 || (size_t)n >= out_size - written) {
            out[0] = '\0';
            return -1;
        }
        written += (size_t)n;
    }

    return (int)written;
}
