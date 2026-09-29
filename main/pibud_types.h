#pragma once

/*
 * Pi Agent Buddy — core data model (ESP-IDF/LVGL independent, host-testable).
 * Ported/adapted from the claude-buddy demo architecture; pi concepts replace
 * Claude's: 4 cycled views + state-driven overlays, agent heartbeat, and
 * approve/deny/interrupt "acts" driven to a computer sidecar.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIBUD_NAME_MAX      32
#define PIBUD_OWNER_MAX     32
#define PIBUD_MODEL_MAX     24
#define PIBUD_TITLE_MAX     32
#define PIBUD_TOOL_MAX      24
#define PIBUD_ARG_MAX       48
#define PIBUD_STOP_MAX      16
#define PIBUD_STATE_MAX     12
#define PIBUD_PROMPT_ID_MAX 64
#define PIBUD_PROMPT_TEXT_MAX 160
#define PIBUD_MESSAGE_MAX   96
#define PIBUD_CMD_MAX       32

typedef enum {
    PIBUD_CONNECTION_OFFLINE,
    PIBUD_CONNECTION_CONNECTED,
    PIBUD_CONNECTION_PAIRING,
    PIBUD_CONNECTION_CONFIRMING,
} pibud_connection_t;

/* Mascot (16px status avatar) semantic states. */
typedef enum {
    PIBUD_CHAR_SLEEP,
    PIBUD_CHAR_IDLE,
    PIBUD_CHAR_BUSY,
    PIBUD_CHAR_ATTENTION,
    PIBUD_CHAR_CELEBRATE,
    PIBUD_CHAR_HEART,
    PIBUD_CHAR_CONFIRM,
    PIBUD_CHAR_PAIR,
} pibud_character_t;

/* The 4 cycled views. APPROVAL / PAIRING are state-driven overlays, not cycled. */
typedef enum {
    PIBUD_VIEW_HOME,
    PIBUD_VIEW_LIVE,
    PIBUD_VIEW_STATS,
    PIBUD_VIEW_MENU,
    PIBUD_VIEW_COUNT,
} pibud_view_t;

typedef enum {
    PIBUD_KEY_NONE,
    PIBUD_KEY_UP,
    PIBUD_KEY_DOWN,
    PIBUD_KEY_OK,
} pibud_key_t;

typedef enum {
    PIBUD_EVENT_NONE,
    PIBUD_EVENT_HEARTBEAT,
    PIBUD_EVENT_PROMPT,
    PIBUD_EVENT_TIME,
    PIBUD_EVENT_NAME,
    PIBUD_EVENT_OWNER,
    PIBUD_EVENT_STATUS_REQUEST,
    PIBUD_EVENT_UNPAIR_CONFIRMATION,
    PIBUD_EVENT_BLE_CONNECTED,
    PIBUD_EVENT_BLE_DISCONNECTED,
    PIBUD_EVENT_BLE_PASSKEY,
    PIBUD_EVENT_BLE_ENCRYPTION,
    PIBUD_EVENT_BOND_DELETE_RESULT,
    PIBUD_EVENT_ACT_RESULT,
    PIBUD_EVENT_KEY_CLICK,
    PIBUD_EVENT_KEY_LONG,
    PIBUD_EVENT_TICK,
} pibud_event_type_t;

typedef enum {
    PIBUD_ACTION_NONE,
    PIBUD_ACTION_UI_REFRESH,
    PIBUD_ACTION_ACT,
    PIBUD_ACTION_SETTINGS,
    PIBUD_ACTION_STATUS,
    PIBUD_ACTION_UNPAIR_CONFIRMED,
    PIBUD_ACTION_FACTORY_RESET_CONFIRMED,
    PIBUD_ACTION_BLE_TOGGLE,
    PIBUD_ACTION_UI_SCROLL,
    PIBUD_ACTION_DISPLAY_BACKLIGHT,
    PIBUD_ACTION_SCREEN_OFF,
} pibud_action_type_t;

/* Device -> sidecar "act" kinds. */
typedef enum {
    PIBUD_ACT_NONE,
    PIBUD_ACT_APPROVE,
    PIBUD_ACT_DENY,
    PIBUD_ACT_INTERRUPT,
} pibud_act_kind_t;

typedef enum {
    PIBUD_DELIVERY_NONE,
    PIBUD_DELIVERY_SENDING,
    PIBUD_DELIVERY_SENT,
    PIBUD_DELIVERY_FAILED,
} pibud_delivery_t;

typedef enum {
    PIBUD_CONFIRM_NONE,
    PIBUD_CONFIRM_UNPAIR,
    PIBUD_CONFIRM_FACTORY_RESET,
} pibud_confirmation_t;

typedef enum {
    PIBUD_SETTING_BRIGHTNESS,
    PIBUD_SETTING_SOUND,
    PIBUD_SETTING_BLE,
    PIBUD_SETTING_TRANSCRIPT,
    PIBUD_SETTING_UNPAIR,
    PIBUD_SETTING_FACTORY_RESET,
    PIBUD_SETTING_COUNT,
} pibud_setting_item_t;

typedef struct {
    char model[PIBUD_MODEL_MAX];
    char title[PIBUD_TITLE_MAX];
    char tool[PIBUD_TOOL_MAX];
    char arg[PIBUD_ARG_MAX];
    char stop_reason[PIBUD_STOP_MAX];
    char state[PIBUD_STATE_MAX];   /* running | idle | offline */
    bool result_ok;
    bool has_result;
    bool thinking;
    unsigned sub_total;
    unsigned sub_working;
    uint64_t tokens;
    double cost;
    bool connected;
} pibud_heartbeat_t;

typedef struct {
    char id[PIBUD_PROMPT_ID_MAX];
    size_t id_length;
    char text[PIBUD_PROMPT_TEXT_MAX];
    bool connected;
    bool id_truncated;
    bool text_truncated;
} pibud_prompt_t;

typedef struct {
    char name[PIBUD_NAME_MAX];
    char owner[PIBUD_OWNER_MAX];
    bool ble_enabled;
    uint64_t celebrated_level;
} pibud_settings_t;

typedef struct {
    pibud_act_kind_t kind;
    char id[PIBUD_PROMPT_ID_MAX];
    char tool[PIBUD_TOOL_MAX];
    uint32_t connection_generation;
} pibud_act_action_t;

typedef struct {
    char id[PIBUD_PROMPT_ID_MAX];
    size_t id_length;
    pibud_act_kind_t kind;
    bool success;
} pibud_act_result_t;

typedef struct {
    uint32_t passkey;
    uint32_t connection_generation;
    int status;
    bool secure;
    bool success;
} pibud_ble_state_event_t;

typedef struct {
    int64_t epoch_seconds;
    int32_t tz_offset_seconds;
} pibud_time_sync_t;

typedef struct {
    char name[PIBUD_CMD_MAX];
    char value[PIBUD_MESSAGE_MAX];
    bool value_truncated;
} pibud_command_t;

typedef struct {
    pibud_event_type_t type;
    pibud_key_t key;
    pibud_heartbeat_t heartbeat;
    pibud_prompt_t prompt;
    pibud_time_sync_t time;
    pibud_command_t command;
    pibud_ble_state_event_t ble;
    pibud_act_result_t act_result;
} pibud_event_t;

typedef struct {
    pibud_action_type_t type;
    pibud_act_action_t act;
    pibud_settings_t settings;
    char message[PIBUD_MESSAGE_MAX];
    int scroll_delta;
    uint8_t brightness_percent;
    uint32_t connection_generation;
    bool ble_enabled;
    bool confirmation_acknowledge;
} pibud_action_t;

/* Immutable UI snapshot (consumed by the LVGL render layer under bsp_lvgl_lock()). */
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
    int64_t epoch_seconds;
    int32_t tz_offset_seconds;
    uint64_t time_received_ms;
    bool approval_visible;     /* overlay: actionable prompt present */
    bool pairing_visible;     /* overlay: passkey visible */
    bool confirmation_pending;
    pibud_confirmation_t confirmation;
    pibud_setting_item_t settings_sel;
    bool menu_open;
    bool screen_off;
    uint8_t brightness_level;
    pibud_delivery_t delivery;
    bool approval_locked;
    bool heartbeat_stale;
    bool ble_connected;
    bool ble_encrypted;
    bool battery_available;
    bool passkey_visible;
    uint32_t passkey;
    uint8_t battery_percent;
    uint16_t battery_mv;
} pibud_ui_snapshot_t;
