// Host tests for the provisioning form decoder (pure logic, no ESP-IDF).
#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "pibud_form.h"

#define GET(body_, key_, out_) pibud_form_get((body_), strlen(body_), (key_), (out_), sizeof(out_))

int main(void)
{
    char out[32];

    /* Value found at the start, middle and end of a body. */
    {
        const char *body = "ssid=amyleo&pass=secret";
        assert(GET(body, "ssid", out) == 6);
        assert(strcmp(out, "amyleo") == 0);
        assert(GET(body, "pass", out) == 6);
        assert(strcmp(out, "secret") == 0);
    }
    {
        const char *body = "a=1&ssid=amyleo&b=2";
        assert(GET(body, "ssid", out) == 6);
        assert(strcmp(out, "amyleo") == 0);
    }
    assert(GET("ssid=amyleo", "ssid", out) == 6);

    /* Absent key, and keys that merely share a prefix. */
    assert(GET("ssid=amyleo", "pass", out) == -1);
    assert(out[0] == '\0');
    assert(GET("ssid_extra=x", "ssid", out) == -1);
    assert(GET("xssid=amyleo", "ssid", out) == -1);
    assert(GET("", "ssid", out) == -1);

    /* A key appearing as a value must not be mistaken for the key itself. */
    {
        const char *body = "a=ssid&ssid=real";
        assert(GET(body, "ssid", out) == 4);
        assert(strcmp(out, "real") == 0);
    }

    /* Present but empty is distinct from absent: open networks have no password. */
    {
        const char *body = "ssid=amyleo&pass=";
        assert(GET(body, "pass", out) == 0);
        assert(strcmp(out, "") == 0);
    }
    assert(GET("ssid", "ssid", out) == 0);

    /* '+' is a space; percent escapes decode with either hex case. */
    assert(GET("pass=a+b+c", "pass", out) == 5);
    assert(strcmp(out, "a b c") == 0);
    assert(GET("pass=%2Fx%2fy", "pass", out) == 4);
    assert(strcmp(out, "/x/y") == 0);
    assert(GET("pass=%2fx%2Fy", "pass", out) == 4);
    assert(strcmp(out, "/x/y") == 0);

    /* Real passwords contain '=' and '&' once encoded; split on the first '='. */
    assert(GET("pass=a=b", "pass", out) == 3);
    assert(strcmp(out, "a=b") == 0);
    assert(GET("pass=a%26b", "pass", out) == 3);
    assert(strcmp(out, "a&b") == 0);

    /* Malformed escapes are rejected rather than stored as literal text. */
    assert(GET("ssid=ab%", "ssid", out) == -1);
    assert(GET("ssid=ab%4", "ssid", out) == -1);
    assert(GET("ssid=ab%zz", "ssid", out) == -1);
    assert(GET("ssid=ab%0", "ssid", out) == -1);
    assert(out[0] == '\0');

    /* An escaped NUL cannot round-trip through a C string. */
    assert(GET("ssid=a%00b", "ssid", out) == -1);
    assert(GET("ssid=%00", "ssid", out) == -1);

    /* A credential that does not fit is refused, never truncated. */
    {
        char small[6];
        assert(GET("pass=secret", "pass", small) == -1);
        assert(small[0] == '\0');
        /* Exactly fitting values still work: 5 chars plus the terminator. */
        assert(GET("pass=abcde", "pass", small) == 5);
        assert(strcmp(small, "abcde") == 0);
    }

    /* Degenerate arguments. */
    assert(pibud_form_get("ssid=a", 6, "ssid", out, sizeof(out)) == 1);
    assert(pibud_form_get(NULL, 0, "ssid", out, sizeof(out)) == -1);
    assert(pibud_form_get("ssid=a", 6, NULL, out, sizeof(out)) == -1);
    assert(pibud_form_get("ssid=a", 6, "ssid", NULL, sizeof(out)) == -1);
    assert(pibud_form_get("ssid=a", 6, "ssid", out, 0) == -1);

    /* A trailing '&' does not create a phantom field. */
    assert(GET("ssid=amyleo&", "ssid", out) == 6);
    assert(GET("ssid=amyleo&", "pass", out) == -1);

    return 0;
}
