// Host tests for the provisioning page HTML builder (pure logic, no ESP-IDF).
#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "pibud_prov_html.h"

static int count_substring(const char *haystack, const char *needle)
{
    int found = 0;
    for (const char *p = haystack; (p = strstr(p, needle)) != NULL; p++) {
        found++;
    }
    return found;
}

// Asserts the escaped form of `input` is exactly `want`, including the reported
// length: deriving it here keeps the expectations readable instead of
// hand-counting the expansion of every entity.
static void assert_escape(const char *input, const char *want)
{
    char buf[256];
    int n = pibud_html_escape(input, buf, sizeof(buf));
    assert(n >= 0);
    assert((size_t)n == strlen(want));
    assert(strcmp(buf, want) == 0);
}

int main(void)
{
    char out[512];

    /* Plain text passes through untouched. */
    assert_escape("amyleo", "amyleo");
    assert_escape("", "");

    /* Every character that can break out of text or an attribute is replaced. */
    assert_escape("a&b", "a&amp;b");
    assert_escape("<x>", "&lt;x&gt;");
    assert_escape("\"q\"", "&quot;q&quot;");
    assert_escape("it's", "it&#39;s");

    /* An SSID chosen by a hostile neighbour must not reach the page as markup. */
    {
        const char *hostile = "</select><script>alert(1)</script>";
        assert(pibud_html_escape(hostile, out, sizeof(out)) > 0);
        assert(strchr(out, '<') == NULL);
        assert(strchr(out, '>') == NULL);
        assert(strchr(out, '\'') == NULL);
        assert(strchr(out, '"') == NULL);
        assert(strstr(out, "&lt;/select&gt;") == out);
    }

    /* Escaping is bounded: it refuses rather than truncating. */
    {
        char small[8];
        assert(pibud_html_escape("a&b&c&d", small, sizeof(small)) == -1);
        assert(small[0] == '\0');
        assert(pibud_html_escape("abcde", small, sizeof(small)) == 5);
        assert(strcmp(small, "abcde") == 0);
    }

    /* Degenerate arguments. */
    assert(pibud_html_escape(NULL, out, sizeof(out)) == -1);
    assert(pibud_html_escape("x", NULL, sizeof(out)) == -1);
    assert(pibud_html_escape("x", out, 0) == -1);

    /* One option per distinct, non-empty SSID. */
    {
        const char *ssids[] = { "one", "two", "one", "", NULL, "three" };
        int n = pibud_prov_options_html(ssids, 6, out, sizeof(out));
        assert(n > 0);
        assert((size_t)n == strlen(out));
        assert(count_substring(out, "<option ") == 3);
        assert(count_substring(out, "</option>") == 3);
        assert(strstr(out, "value=\"one\">one</option>") != NULL);
        assert(strstr(out, "value=\"two\">two</option>") != NULL);
        assert(strstr(out, "value=\"three\">three</option>") != NULL);
    }

    /* Option values are escaped in both the attribute and the label. */
    {
        const char *ssids[] = { "a\"<b>" };
        assert(pibud_prov_options_html(ssids, 1, out, sizeof(out)) > 0);
        assert(strstr(out, "value=\"a&quot;&lt;b&gt;\">a&quot;&lt;b&gt;</option>") != NULL);
        assert(count_substring(out, "<option ") == 1);
    }

    /* No networks found is not an error; it yields an empty list. */
    {
        const char *none[] = { "", NULL };
        assert(pibud_prov_options_html(none, 2, out, sizeof(out)) == 0);
        assert(strcmp(out, "") == 0);
        assert(pibud_prov_options_html(NULL, 0, out, sizeof(out)) == 0);
    }

    /* A list that cannot fit is refused as a whole. */
    {
        const char *ssids[] = { "one", "two", "three" };
        char small[32];
        assert(pibud_prov_options_html(ssids, 3, small, sizeof(small)) == -1);
        assert(small[0] == '\0');
    }

    /* Degenerate arguments. */
    assert(pibud_prov_options_html(NULL, 1, out, sizeof(out)) == -1);
    assert(pibud_prov_options_html(NULL, 0, NULL, sizeof(out)) == -1);
    assert(pibud_prov_options_html(NULL, 0, out, 0) == -1);

    return 0;
}
