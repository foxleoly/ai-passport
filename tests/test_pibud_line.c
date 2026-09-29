#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "pibud_line.h"

typedef struct {
    int count;
    char last[256];
    int last_len;
} line_ctx_t;

static bool capture(const char *line, size_t length, void *context)
{
    line_ctx_t *ctx = context;
    ctx->count++;
    if (length < sizeof(ctx->last)) {
        memcpy(ctx->last, line, length);
        ctx->last[length] = '\0';
    }
    ctx->last_len = (int)length;
    return true;
}

int main(void)
{
    pibud_line_buffer_t buf;
    line_ctx_t ctx = {0};
    const char *msg = "{\"a\":1}\n{\"b\":2}\n";

    pibud_line_init(&buf);
    pibud_line_push(&buf, (const uint8_t *)msg, strlen(msg), capture, &ctx);
    assert(ctx.count == 2);
    assert(strcmp(ctx.last, "{\"b\":2}") == 0);

    /* Overflow: a line longer than PIBUD_JSON_LINE_MAX is discarded. */
    pibud_line_init(&buf);
    ctx.count = 0;
    {
        char big[PIBUD_JSON_LINE_MAX + 8];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\n';
        pibud_line_result_t res = pibud_line_push(&buf, (const uint8_t *)big, sizeof(big),
                                                   capture, &ctx);
        assert(res == PIBUD_LINE_OVERFLOW);
        assert(ctx.count == 0); /* the oversized line never produced a callback */
    }

    /* Next valid line after overflow still works. */
    {
        const char *after = "ok\n";
        pibud_line_result_t res = pibud_line_push(&buf, (const uint8_t *)after, strlen(after),
                                                   capture, &ctx);
        assert(res == PIBUD_LINE_OK);
        assert(ctx.count == 1);
        assert(strcmp(ctx.last, "ok") == 0);
    }

    return 0;
}
