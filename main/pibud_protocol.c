#include "pibud_protocol.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static void pibud_copy_str(const cJSON *root, const char *key, char *dst, size_t dst_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);

    if (dst_size == 0) {
        return;
    }
    dst[0] = '\0';
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        size_t n = strlen(item->valuestring);
        if (n >= dst_size) {
            n = dst_size - 1;
        }
        memcpy(dst, item->valuestring, n);
        dst[n] = '\0';
    }
}

static int pibud_parse_heartbeat(const cJSON *root, pibud_event_t *event)
{
    pibud_heartbeat_t *hb = &event->heartbeat;

    event->type = PIBUD_EVENT_HEARTBEAT;
    hb->connected = true;
    pibud_copy_str(root, "model", hb->model, sizeof(hb->model));
    pibud_copy_str(root, "title", hb->title, sizeof(hb->title));
    pibud_copy_str(root, "tool", hb->tool, sizeof(hb->tool));
    pibud_copy_str(root, "arg", hb->arg, sizeof(hb->arg));
    pibud_copy_str(root, "stop_reason", hb->stop_reason, sizeof(hb->stop_reason));
    pibud_copy_str(root, "state", hb->state, sizeof(hb->state));
    if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "tokens"))) {
        hb->tokens = (uint64_t)cJSON_GetObjectItemCaseSensitive(root, "tokens")->valuedouble;
    }
    if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "cost"))) {
        hb->cost = cJSON_GetObjectItemCaseSensitive(root, "cost")->valuedouble;
    }
    {
        const cJSON *it = cJSON_GetObjectItemCaseSensitive(root, "sub_total");
        hb->sub_total = it ? (unsigned)it->valueint : 0;
        it = cJSON_GetObjectItemCaseSensitive(root, "sub_working");
        hb->sub_working = it ? (unsigned)it->valueint : 0;
    }
    {
        const cJSON *rok = cJSON_GetObjectItemCaseSensitive(root, "result_ok");
        hb->has_result = (rok != NULL);
        hb->result_ok = cJSON_IsTrue(rok);
    }
    hb->thinking = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "thinking"));
    return PIBUD_PROTO_OK;
}

static int pibud_parse_prompt(const cJSON *root, pibud_event_t *event)
{
    const cJSON *id;
    pibud_prompt_t *prompt = &event->prompt;
    size_t n;

    event->type = PIBUD_EVENT_PROMPT;
    prompt->connected = true;
    id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(id) && id->valuestring != NULL) {
        n = strlen(id->valuestring);
        if (n >= PIBUD_PROMPT_ID_MAX) {
            prompt->id_truncated = true;
            n = PIBUD_PROMPT_ID_MAX - 1;
        }
        memcpy(prompt->id, id->valuestring, n);
        prompt->id[n] = '\0';
        prompt->id_length = n;
    }
    {
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
        if (cJSON_IsString(text) && text->valuestring != NULL) {
            size_t t = strlen(text->valuestring);
            if (t >= PIBUD_PROMPT_TEXT_MAX) {
                prompt->text_truncated = true;
                t = PIBUD_PROMPT_TEXT_MAX - 1;
            }
            memcpy(prompt->text, text->valuestring, t);
            prompt->text[t] = '\0';
        }
    }
    return PIBUD_PROTO_OK;
}

int pibud_protocol_parse(const char *json, pibud_event_t *event)
{
    cJSON *root;
    const cJSON *cmd;
    const char *name;

    if (json == NULL || event == NULL) {
        return PIBUD_PROTO_MALFORMED;
    }
    memset(event, 0, sizeof(*event));

    root = cJSON_Parse(json);
    if (root == NULL) {
        return PIBUD_PROTO_MALFORMED;
    }
    cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cJSON_IsString(cmd) || cmd->valuestring == NULL) {
        cJSON_Delete(root);
        return PIBUD_PROTO_MALFORMED;
    }
    name = cmd->valuestring;

    if (strcmp(name, "hb") == 0) {
        int rc = pibud_parse_heartbeat(root, event);
        cJSON_Delete(root);
        return rc;
    }
    if (strcmp(name, "prompt") == 0) {
        int rc = pibud_parse_prompt(root, event);
        cJSON_Delete(root);
        return rc;
    }
    if (strcmp(name, "name") == 0) {
        event->type = PIBUD_EVENT_NAME;
        strcpy(event->command.name, "name");
        pibud_copy_str(root, "value", event->command.value, sizeof(event->command.value));
        cJSON_Delete(root);
        return PIBUD_PROTO_OK;
    }
    if (strcmp(name, "owner") == 0) {
        event->type = PIBUD_EVENT_OWNER;
        strcpy(event->command.name, "owner");
        pibud_copy_str(root, "value", event->command.value, sizeof(event->command.value));
        cJSON_Delete(root);
        return PIBUD_PROTO_OK;
    }
    if (strcmp(name, "time") == 0) {
        const cJSON *epoch = cJSON_GetObjectItemCaseSensitive(root, "epoch");
        const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "tz");

        event->type = PIBUD_EVENT_TIME;
        event->time.epoch_seconds = epoch ? (int64_t)epoch->valuedouble : 0;
        event->time.tz_offset_seconds = tz ? (int32_t)tz->valueint : 0;
        cJSON_Delete(root);
        return PIBUD_PROTO_OK;
    }
    if (strcmp(name, "status_req") == 0) {
        event->type = PIBUD_EVENT_STATUS_REQUEST;
        cJSON_Delete(root);
        return PIBUD_PROTO_OK;
    }
    if (strcmp(name, "unpair") == 0) {
        event->type = PIBUD_EVENT_UNPAIR_CONFIRMATION;
        cJSON_Delete(root);
        return PIBUD_PROTO_OK;
    }
    cJSON_Delete(root);
    return PIBUD_PROTO_UNKNOWN;
}

int pibud_protocol_act_json(char *out, size_t size, pibud_act_kind_t kind,
                           const char *id, const char *tool)
{
    static const char *const kind_name[4] = {"none", "approve", "deny", "interrupt"};
    int wrote;

    if (out == NULL || size == 0 || (unsigned)kind > 3U ||
        id == NULL || tool == NULL) {
        return -1;
    }
    wrote = snprintf(out, size, "{\"cmd\":\"act\",\"kind\":\"%s\",\"id\":\"%s\","
                                "\"tool\":\"%s\"}",
                     kind_name[kind], id, tool);
    if (wrote < 0 || (size_t)wrote >= size) {
        return -2;
    }
    return wrote;
}
