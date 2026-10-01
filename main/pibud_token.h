// main/pibud_token.h — link-code handling for the WebSocket sidecar link.
//
// The code is generated and printed by the sidecar and typed on a phone, so it
// arrives with whatever separators and capitalisation the keyboard produced.
// Normalising is kept free of ESP-IDF so the rules are host-testable.
#ifndef PIBUD_TOKEN_H
#define PIBUD_TOKEN_H

#include <stddef.h>

// Hex digits in a link code.
#define PIBUD_TOKEN_LEN 12

// Normalises a typed link code to lower-case hex digits.
//
// Separators ('-', '_', ':', spaces and tabs) are ignored so a code shown as
// groups can be typed with or without them. Returns PIBUD_TOKEN_LEN and writes a
// terminated copy, or -1 when the input does not hold exactly PIBUD_TOKEN_LEN hex
// digits, contains another character, or does not fit in `out`.
int pibud_token_normalize(const char *input, char *out, size_t out_size);

#endif
