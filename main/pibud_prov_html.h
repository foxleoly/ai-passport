// main/pibud_prov_html.h — HTML building for the provisioning page.
//
// SSIDs are attacker-influenced: any nearby access point picks its own name, and
// that name is rendered into a page the user's phone displays. Everything
// interpolated here is escaped, and the escaping stays free of ESP-IDF so it can
// be covered by host tests.
#ifndef PIBUD_PROV_HTML_H
#define PIBUD_PROV_HTML_H

#include <stddef.h>

// Escapes `value` for HTML text and double-quoted attribute contexts.
//
// Returns the escaped length, or -1 when the result does not fit in `out`
// including its terminator, in which case `out` is set to the empty string.
int pibud_html_escape(const char *value, char *out, size_t out_size);

// Writes one `<option>` element per distinct, non-empty SSID in `ssids`.
//
// Returns the written length, or -1 when the result does not fit in `out`. An
// entry whose escaped form is too long to be a Wi-Fi SSID is skipped rather than
// failing the whole list. A NULL list with a count of 0 yields an empty list,
// because finding no networks is a normal outcome, not an error.
int pibud_prov_options_html(const char *const *ssids, size_t count,
                            char *out, size_t out_size);

#endif
