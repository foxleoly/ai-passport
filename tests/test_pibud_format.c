#include <assert.h>
#include <string.h>

#include "pibud_format.h"

static void expect(uint64_t tokens, const char *want)
{
    char buf[PIBUD_FORMAT_TOKENS_MAX];
    pibud_format_tokens(buf, sizeof(buf), tokens);
    assert(strcmp(buf, want) == 0);
}

int main(void)
{
    /* raw below a thousand */
    expect(0, "0");
    expect(7, "7");
    expect(999, "999");

    /* thousands */
    expect(1000, "1K");
    expect(12345, "12.3K");
    expect(999999, "999.9K");

    /* millions */
    expect(1000000, "1M");
    expect(1234567, "1.2M");
    expect(2500000, "2.5M");

    /* billions */
    expect(1000000000, "1B");
    expect(3400000000ULL, "3.4B");

    /* trillions */
    expect(1000000000000ULL, "1T");
    expect(1234000000000ULL, "1.2T");

    /* a short buffer truncates but still terminates */
    char small[4];
    pibud_format_tokens(small, sizeof(small), 12345678);
    assert(small[sizeof(small) - 1] == '\0');

    /* per-bucket percentages */
    assert(pibud_percent(18, 100) == 18);
    assert(pibud_percent(500, 1500) == 33);
    assert(pibud_percent(850, 1500) == 56);
    assert(pibud_percent(2393419, 145668116) == 1);
    assert(pibud_percent(0, 0) == 0);
    assert(pibud_percent(5, 0) == 0);

    /* local clock from epoch + tz offset. 1759276800 is UTC midnight, so the
     * epoch below is 13:00 UTC. */
    char clock[PIBUD_FORMAT_CLOCK_MAX];
    pibud_format_clock(clock, sizeof(clock), 0, 0);
    assert(strcmp(clock, "--:--") == 0); /* no time yet */
    pibud_format_clock(clock, sizeof(clock), 1759323600, 8 * 3600);
    assert(strcmp(clock, "21:00") == 0);
    pibud_format_clock(clock, sizeof(clock), 1759323600, 0);
    assert(strcmp(clock, "13:00") == 0);
    /* 02:00 UTC, seven hours behind -> 19:00 the previous day. */
    pibud_format_clock(clock, sizeof(clock), 1759284000, -7 * 3600);
    assert(strcmp(clock, "19:00") == 0);

    return 0;
}
