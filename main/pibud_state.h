#pragma once

#include "pibud_types.h"

typedef struct {
    pibud_connection_t connection;
    pibud_character_t character;
    pibud_view_t view;
    pibud_heartbeat_t hb;
    pibud_prompt_t prompt;
    pibud_settings_t settings;
    char name[PIBUD_NAME_MAX];
    char owner[PIBUD_OWNER_MAX];
    char message[PIBUD_MESSAGE_MAX];
    int scroll;
    char last_attempted_id[PIBUD_PROMPT_ID_MAX];
    char last_sent_id[PIBUD_PROMPT_ID_MAX];
    int64_t epoch_seconds;
    int32_t tz_offset_seconds;
    uint64_t time_received_ms;
    uint64_t last_heartbeat_ms;
    uint64_t temporary_until_ms;
    uint32_t prompt_generation;
    uint32_t confirmation_generation;
    uint32_t ble_generation;
    pibud_character_t temporary_character;
    bool connected;
    bool heartbeat_stale;
    bool confirmation_pending;
    pibud_confirmation_t confirmation;
    bool confirmation_acknowledge;
    pibud_setting_item_t settings_sel;
    uint8_t brightness_level;
    bool screen_off;
    pibud_delivery_t delivery;
    uint64_t delivery_until_ms;
    bool approval_locked;
    bool ble_connected;
    bool ble_encrypted;
    bool battery_available;
    bool passkey_visible;
    uint32_t passkey;
    uint8_t battery_percent;
    uint16_t battery_mv;
} pibud_state_t;

void pibud_state_init(pibud_state_t *state, const pibud_settings_t *settings);
void pibud_state_reduce(pibud_state_t *state, const pibud_event_t *event,
                        uint64_t now_ms, pibud_action_t *action);
void pibud_state_snapshot(const pibud_state_t *state, pibud_ui_snapshot_t *snapshot);
