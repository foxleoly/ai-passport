#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Bounded JSON line buffer; oversized lines are discarded to the next newline. */
#define PIBUD_JSON_LINE_MAX 4096

typedef enum {
    PIBUD_LINE_OK,
    PIBUD_LINE_OVERFLOW,
    PIBUD_LINE_ABORTED,
} pibud_line_result_t;

typedef bool (*pibud_line_callback_t)(const char *line, size_t length, void *context);

typedef struct {
    char data[PIBUD_JSON_LINE_MAX + 1];
    size_t length;
    bool discarding;
} pibud_line_buffer_t;

void pibud_line_init(pibud_line_buffer_t *buffer);
pibud_line_result_t pibud_line_push(pibud_line_buffer_t *buffer, const uint8_t *bytes,
                                    size_t length, pibud_line_callback_t callback,
                                    void *context);
