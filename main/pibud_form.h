// main/pibud_form.h — application/x-www-form-urlencoded field extraction.
//
// Used by the SoftAP provisioning page. Deliberately free of ESP-IDF so the
// decoding rules (percent escapes, '+' as space, bounded output) stay
// host-testable.
#ifndef PIBUD_FORM_H
#define PIBUD_FORM_H

#include <stddef.h>

// Copies the decoded value of `key` from a form body into `out`.
//
// Keys are matched literally (never percent-decoded), because the field names
// this firmware reads are plain ASCII and a browser does not encode them.
//
// Returns the decoded length, which is 0 for a present-but-empty value, or -1
// when the key is absent, an escape is malformed, an escape decodes to NUL, or
// the value does not fit in `out` including its terminator. Credentials are
// never truncated: an oversized value is rejected so a caller cannot silently
// store half a password. On -1, `out` is set to the empty string.
int pibud_form_get(const char *body, size_t len, const char *key, char *out, size_t out_size);

#endif
