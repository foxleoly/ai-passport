#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "pibud_protocol.h"
#include "pibud_types.h"

int main(void)
{
    pibud_event_t e;

    /* Heartbeat */
    memset(&e, 0, sizeof(e));
    assert(pibud_protocol_parse(
               "{\"cmd\":\"hb\",\"model\":\"agnes-3.0-flash\",\"state\":\"running\","
               "\"tool\":\"edit\",\"arg\":\"main/x.c\",\"result_ok\":true,\"thinking\":true,"
               "\"sub_total\":4,\"sub_working\":1,\"tokens\":18432,\"cost\":0.06,"
               "\"stop_reason\":\"toolUse\",\"title\":\"pi - x\"}",
               &e) == PIBUD_PROTO_OK);
    assert(e.type == PIBUD_EVENT_HEARTBEAT);
    assert(e.heartbeat.connected);
    assert(strcmp(e.heartbeat.model, "agnes-3.0-flash") == 0);
    assert(strcmp(e.heartbeat.state, "running") == 0);
    assert(strcmp(e.heartbeat.tool, "edit") == 0);
    assert(strcmp(e.heartbeat.arg, "main/x.c") == 0);
    assert(e.heartbeat.result_ok && e.heartbeat.has_result);
    assert(e.heartbeat.thinking);
    assert(e.heartbeat.sub_total == 4 && e.heartbeat.sub_working == 1);
    assert(e.heartbeat.tokens == 18432);
    assert(e.heartbeat.cost > 0.05 && e.heartbeat.cost < 0.07);
    assert(strcmp(e.heartbeat.stop_reason, "toolUse") == 0);

    /* Heartbeat with no optional numbers (bounded input) */
    memset(&e, 0, sizeof(e));
    assert(pibud_protocol_parse("{\"cmd\":\"hb\",\"state\":\"idle\"}", &e) ==
           PIBUD_PROTO_OK);
    assert(e.type == PIBUD_EVENT_HEARTBEAT);
    assert(e.heartbeat.sub_total == 0 && e.heartbeat.tokens == 0);
    assert(!e.heartbeat.has_result);

    /* Prompt with valid short id */
    memset(&e, 0, sizeof(e));
    assert(pibud_protocol_parse(
               "{\"cmd\":\"prompt\",\"id\":\"p1\",\"text\":\"flash fw?\",\"waiting\":true}",
               &e) == PIBUD_PROTO_OK);
    assert(e.type == PIBUD_EVENT_PROMPT);
    assert(e.prompt.connected);
    assert(e.prompt.id_length == 2 && memcmp(e.prompt.id, "p1", 2) == 0);
    assert(!e.prompt.id_truncated);
    assert(strcmp(e.prompt.text, "flash fw?") == 0);

    /* Prompt with over-long id -> truncated + flagged */
    memset(&e, 0, sizeof(e));
    {
        char long_id[PIBUD_PROMPT_ID_MAX + 8];
        memset(long_id, 'a', sizeof(long_id) - 1);
        long_id[sizeof(long_id) - 1] = '\0';
        char json[PIBUD_PROMPT_ID_MAX + 128];
        snprintf(json, sizeof(json), "{\"cmd\":\"prompt\",\"id\":\"%s\"}", long_id);
        assert(pibud_protocol_parse(json, &e) == PIBUD_PROTO_OK);
        assert(e.type == PIBUD_EVENT_PROMPT);
        assert(e.prompt.id_truncated);
        assert(e.prompt.id_length == PIBUD_PROMPT_ID_MAX - 1);
    }

    /* Commands */
    memset(&e, 0, sizeof(e));
    assert(pibud_protocol_parse("{\"cmd\":\"name\",\"value\":\"Pi-7F3A\"}", &e) ==
           PIBUD_PROTO_OK);
    assert(e.type == PIBUD_EVENT_NAME);
    assert(strcmp(e.command.value, "Pi-7F3A") == 0);

    memset(&e, 0, sizeof(e));
    assert(pibud_protocol_parse("{\"cmd\":\"time\",\"epoch\":123,\"tz\":-28800}", &e) ==
           PIBUD_PROTO_OK);
    assert(e.type == PIBUD_EVENT_TIME);
    assert(e.time.epoch_seconds == 123 && e.time.tz_offset_seconds == -28800);

    memset(&e, 0, sizeof(e));
    assert(pibud_protocol_parse("{\"cmd\":\"status_req\"}", &e) == PIBUD_PROTO_OK);
    assert(e.type == PIBUD_EVENT_STATUS_REQUEST);

    memset(&e, 0, sizeof(e));
    assert(pibud_protocol_parse("{\"cmd\":\"unpair\"}", &e) == PIBUD_PROTO_OK);
    assert(e.type == PIBUD_EVENT_UNPAIR_CONFIRMATION);

    /* Unknown + malformed */
    memset(&e, 0, sizeof(e));
    assert(pibud_protocol_parse("{\"cmd\":\"frobnicate\"}", &e) == PIBUD_PROTO_UNKNOWN);
    assert(pibud_protocol_parse("not json", &e) == PIBUD_PROTO_MALFORMED);
    assert(pibud_protocol_parse("", &e) == PIBUD_PROTO_MALFORMED);

    /* act JSON build */
    {
        char buf[256];
        int n = pibud_protocol_act_json(buf, sizeof(buf), PIBUD_ACT_APPROVE, "p1", "edit");
        assert(n > 0 && (size_t)n < sizeof(buf));
        assert(strstr(buf, "\"kind\":\"approve\"") != NULL);
        assert(strstr(buf, "\"id\":\"p1\"") != NULL);
        assert(strstr(buf, "\"tool\":\"edit\"") != NULL);
        n = pibud_protocol_act_json(buf, sizeof(buf), PIBUD_ACT_INTERRUPT, "", "edit");
        assert(n > 0 && strstr(buf, "\"kind\":\"interrupt\"") != NULL);
    }

    return 0;
}
