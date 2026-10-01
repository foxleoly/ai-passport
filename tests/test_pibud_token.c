// Host tests for link-code normalisation (pure logic, no ESP-IDF).
#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "pibud_token.h"

static void assert_normalizes(const char *input, const char *want)
{
    char out[PIBUD_TOKEN_LEN + 1];
    int n = pibud_token_normalize(input, out, sizeof(out));
    assert(n == PIBUD_TOKEN_LEN);
    assert(strcmp(out, want) == 0);
}

int main(void)
{
    char out[PIBUD_TOKEN_LEN + 1];

    /* The canonical form the sidecar prints. */
    assert_normalizes("1a2b-3c4d-5e6f", "1a2b3c4d5e6f");
    assert_normalizes("1a2b3c4d5e6f", "1a2b3c4d5e6f");

    /* Phone keyboards capitalise the first letter, so case must not matter. */
    assert_normalizes("1A2B3C4D5E6F", "1a2b3c4d5e6f");
    assert_normalizes("1A2b-3C4d-5E6f", "1a2b3c4d5e6f");

    /* Separators are ignored wherever they land. */
    assert_normalizes("1a2b 3c4d 5e6f", "1a2b3c4d5e6f");
    assert_normalizes("1a2b_3c4d:5e6f", "1a2b3c4d5e6f");
    assert_normalizes("  1a2b3c4d5e6f  ", "1a2b3c4d5e6f");
    assert_normalizes("-1-a-2-b-3-c-4-d-5-e-6-f-", "1a2b3c4d5e6f");

    /* Wrong lengths and foreign characters are rejected, not guessed at. */
    assert(pibud_token_normalize("1a2b-3c4d", out, sizeof(out)) == -1);
    assert(pibud_token_normalize("1a2b-3c4d-5e6f-7a", out, sizeof(out)) == -1);
    assert(pibud_token_normalize("1a2b-3c4d-5e6g", out, sizeof(out)) == -1);
    assert(pibud_token_normalize("1a2b-3c4d-5e6", out, sizeof(out)) == -1);
    assert(pibud_token_normalize("", out, sizeof(out)) == -1);
    assert(pibud_token_normalize("-----------", out, sizeof(out)) == -1);
    assert(out[0] == '\0');

    /* The caller must provide room for the terminator. */
    {
        char tight[PIBUD_TOKEN_LEN];
        assert(pibud_token_normalize("1a2b3c4d5e6f", tight, sizeof(tight)) == -1);
    }
    assert(pibud_token_normalize("1a2b3c4d5e6f", out, 0) == -1);
    assert(pibud_token_normalize(NULL, out, sizeof(out)) == -1);
    assert(pibud_token_normalize("1a2b3c4d5e6f", NULL, sizeof(out)) == -1);

    return 0;
}
