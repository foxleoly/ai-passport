#pragma once

#include <stddef.h>

#include "pibud_types.h"

#define PIBUD_PROTO_OK 0
#define PIBUD_PROTO_MALFORMED (-1)
#define PIBUD_PROTO_UNKNOWN (-2)

/* Parse one newline-terminated JSON command from the sidecar into an event.
 * Bounded: input is a single < 4096-byte line (pibud_line). Uses cJSON; the
 * parse tree is freed before return. */
int pibud_protocol_parse(const char *json, pibud_event_t *event);

/* Build a device -> sidecar "act" JSON (approve/deny/interrupt).
 * Returns bytes written (excluding NUL) or a negative code on overflow. */
int pibud_protocol_act_json(char *out, size_t size, pibud_act_kind_t kind,
                           const char *id, const char *tool);
