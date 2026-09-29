#include "pibud_state.h"

#include <stddef.h>
#include <string.h>

#define PIBUD_HEARTBEAT_TIMEOUT_MS 30000
#define PIBUD_HEART_ANIMATION_MS   5000
#define PIBUD_CELEBRATION_MS       1500
#define PIBUD_TOKEN_CELEBRATION_STEP 50000
#define PIBUD_SCROLL_STEP          24
#define PIBUD_BRIGHTNESS_STEPS    5

static void pibud_copy(char *dst, size_t dst_size, const char *src)
{
    size_t length = 0;

    if (dst_size == 0) {
        return;
    }
    if (src != NULL) {
        while (length + 1 < dst_size && src[length] != '\0') {
            ++length;
        }
        memcpy(dst, src, length);
    }
    dst[length] = '\0';
}

static bool pibud_matches_length(const char *value, size_t value_size, size_t length)
{
    size_t index;

    if (length == 0 || length >= value_size) {
        return false;
    }
    for (index = 0; index < value_size; ++index) {
        if (value[index] == '\0') {
            return index == length;
        }
    }
    return false;
}

static bool pibud_prompt_valid(const pibud_prompt_t *prompt)
{
    return !prompt->id_truncated &&
           pibud_matches_length(prompt->id, PIBUD_PROMPT_ID_MAX, prompt->id_length);
}

static void pibud_invalidate_prompt(pibud_state_t *state)
{
    memset(&state->prompt, 0, sizeof(state->prompt));
    state->prompt_generation = 0;
    state->approval_locked = false;
}

static void pibud_clear_session(pibud_state_t *state)
{
    state->connected = false;
    state->connection = PIBUD_CONNECTION_OFFLINE;
    memset(&state->hb, 0, sizeof(state->hb));
    pibud_invalidate_prompt(state);
}

static bool pibud_has_actionable_prompt(const pibud_state_t *state)
{
    return state->prompt.id[0] != '\0' && !state->heartbeat_stale &&
           !state->approval_locked && pibud_prompt_valid(&state->prompt);
}

static pibud_character_t pibud_character_for(const pibud_state_t *state, uint64_t now_ms)
{
    bool has_prompt = pibud_has_actionable_prompt(state);

    if (state->confirmation_pending || state->connection == PIBUD_CONNECTION_CONFIRMING) {
        return PIBUD_CHAR_CONFIRM;
    }
    if (state->passkey_visible || state->connection == PIBUD_CONNECTION_PAIRING) {
        return PIBUD_CHAR_PAIR;
    }
    if (has_prompt) {
        return PIBUD_CHAR_ATTENTION;
    }
    if (state->temporary_until_ms > now_ms) {
        return state->temporary_character;
    }
    if (strcmp(state->hb.state, "running") == 0 || state->hb.sub_working > 0) {
        return PIBUD_CHAR_BUSY;
    }
    if (state->connected && !state->heartbeat_stale) {
        return PIBUD_CHAR_IDLE;
    }
    return PIBUD_CHAR_SLEEP;
}

static void pibud_refresh_character(pibud_state_t *state, uint64_t now_ms)
{
    state->character = pibud_character_for(state, now_ms);
}

static void pibud_clear_stale(pibud_state_t *state, uint64_t now_ms)
{
    if (!state->heartbeat_stale &&
        now_ms - state->last_heartbeat_ms >= PIBUD_HEARTBEAT_TIMEOUT_MS) {
        state->heartbeat_stale = true;
        pibud_clear_session(state);
    }
}

static void pibud_ui_refresh(pibud_action_t *action)
{
    if (action != NULL) {
        action->type = PIBUD_ACTION_UI_REFRESH;
    }
}

static void pibud_open_confirmation(pibud_state_t *state, pibud_confirmation_t confirmation,
                                    bool acknowledge, uint32_t generation,
                                    pibud_action_t *action)
{
    pibud_invalidate_prompt(state);
    state->confirmation = confirmation;
    state->confirmation_pending = confirmation != PIBUD_CONFIRM_NONE;
    state->confirmation_acknowledge = acknowledge;
    state->confirmation_generation = generation;
    state->connection = PIBUD_CONNECTION_CONFIRMING;
    pibud_ui_refresh(action);
}

static void pibud_close_confirmation(pibud_state_t *state)
{
    state->confirmation = PIBUD_CONFIRM_NONE;
    state->confirmation_pending = false;
    state->confirmation_acknowledge = false;
    state->confirmation_generation = 0;
    state->connection = state->connected ? PIBUD_CONNECTION_CONNECTED
                                         : PIBUD_CONNECTION_OFFLINE;
}

static bool pibud_prompt_attempted(const pibud_state_t *state, const pibud_prompt_t *prompt)
{
    return pibud_matches_length(state->last_attempted_id, PIBUD_PROMPT_ID_MAX,
                                prompt->id_length) &&
           memcmp(state->last_attempted_id, prompt->id, prompt->id_length) == 0;
}

static void pibud_decide_act(pibud_state_t *state, pibud_act_kind_t kind,
                             pibud_action_t *action)
{
    if (kind == PIBUD_ACT_APPROVE || kind == PIBUD_ACT_DENY) {
        /* Approve/deny need a valid, actionable, un-attempted prompt. */
        if (state->approval_locked || state->prompt.id[0] == '\0' ||
            state->heartbeat_stale || !pibud_prompt_valid(&state->prompt) ||
            pibud_prompt_attempted(state, &state->prompt)) {
            return;
        }
    }
    /* INTERRUPT is a global act: valid whether or not a prompt is pending. */
    if (action != NULL) {
        action->type = PIBUD_ACTION_ACT;
        action->act.kind = kind;
        pibud_copy(action->act.id, sizeof(action->act.id), state->prompt.id);
        pibud_copy(action->act.tool, sizeof(action->act.tool), state->hb.tool);
        action->act.connection_generation = state->prompt_generation;
    }
    pibud_copy(state->last_attempted_id, sizeof(state->last_attempted_id), state->prompt.id);
    state->approval_locked = (kind != PIBUD_ACT_INTERRUPT);
    state->delivery = PIBUD_DELIVERY_SENDING;
}

static void pibud_apply_act_result(pibud_state_t *state,
                                   const pibud_act_result_t *result, uint64_t now_ms,
                                   pibud_action_t *action)
{
    if (state->delivery != PIBUD_DELIVERY_SENDING ||
        !pibud_matches_length(result->id, PIBUD_PROMPT_ID_MAX, result->id_length) ||
        !pibud_matches_length(state->last_attempted_id, PIBUD_PROMPT_ID_MAX,
                              result->id_length) ||
        memcmp(state->last_attempted_id, result->id, result->id_length) != 0) {
        return;
    }
    if (result->success) {
        pibud_copy(state->last_sent_id, sizeof(state->last_sent_id), result->id);
        state->delivery = PIBUD_DELIVERY_SENT;
        if (result->kind == PIBUD_ACT_APPROVE) {
            state->temporary_character = PIBUD_CHAR_HEART;
            state->temporary_until_ms = now_ms + PIBUD_HEART_ANIMATION_MS;
        }
    } else {
        state->delivery = PIBUD_DELIVERY_FAILED;
    }
    pibud_ui_refresh(action);
}

static void pibud_apply_heartbeat(pibud_state_t *state, const pibud_heartbeat_t *hb,
                                  uint32_t generation, uint64_t now_ms,
                                  pibud_action_t *action)
{
    uint64_t level = hb->tokens / PIBUD_TOKEN_CELEBRATION_STEP;

    state->hb = *hb;
    state->connected = hb->connected;
    state->connection =
        hb->connected ? PIBUD_CONNECTION_CONNECTED : PIBUD_CONNECTION_OFFLINE;
    state->heartbeat_stale = !hb->connected;

    if (!state->connected || state->prompt.id[0] == '\0') {
        /* Heartbeat with no active prompt clears any stale actionable prompt. */
        if (state->prompt.id[0] != '\0' &&
            (state->approval_locked || !pibud_prompt_valid(&state->prompt))) {
            pibud_invalidate_prompt(state);
        }
    } else {
        state->prompt_generation = generation;
    }

    state->last_heartbeat_ms = now_ms;

    if (level > state->settings.celebrated_level) {
        state->settings.celebrated_level = level;
        state->temporary_character = PIBUD_CHAR_CELEBRATE;
        state->temporary_until_ms = now_ms + PIBUD_CELEBRATION_MS;
        if (action != NULL) {
            action->type = PIBUD_ACTION_SETTINGS;
            action->settings = state->settings;
        }
        return;
    }
    pibud_ui_refresh(action);
}

static void pibud_apply_prompt(pibud_state_t *state, const pibud_prompt_t *prompt,
                               uint32_t generation, uint64_t now_ms,
                               pibud_action_t *action)
{
    if (!prompt->connected || !pibud_prompt_valid(prompt)) {
        return;
    }
    state->prompt = *prompt;
    state->prompt_generation = generation;
    state->approval_locked = false;
    state->delivery = PIBUD_DELIVERY_NONE;
    state->connected = true;
    state->connection = PIBUD_CONNECTION_CONNECTED;
    state->heartbeat_stale = false;
    state->last_heartbeat_ms = now_ms;
    pibud_ui_refresh(action);
}

static void pibud_view_cycle(pibud_state_t *state, pibud_action_t *action)
{
    state->view = (pibud_view_t)((state->view + 1U) % PIBUD_VIEW_COUNT);
    pibud_ui_refresh(action);
}

static void pibud_apply_setting(pibud_state_t *state, pibud_action_t *action)
{
    switch (state->settings_sel) {
    case PIBUD_SETTING_BRIGHTNESS:
        state->brightness_level = (uint8_t)((state->brightness_level + 1U) %
                                             PIBUD_BRIGHTNESS_STEPS);
        if (action != NULL) {
            action->type = PIBUD_ACTION_DISPLAY_BACKLIGHT;
            action->brightness_percent =
                (uint8_t)(20U + state->brightness_level * 20U);
        }
        break;
    case PIBUD_SETTING_BLE:
        state->settings.ble_enabled = !state->settings.ble_enabled;
        if (action != NULL) {
            action->type = PIBUD_ACTION_BLE_TOGGLE;
            action->ble_enabled = state->settings.ble_enabled;
        }
        break;
    case PIBUD_SETTING_UNPAIR:
        pibud_open_confirmation(state, PIBUD_CONFIRM_UNPAIR, false, 0, action);
        return;
    case PIBUD_SETTING_FACTORY_RESET:
        pibud_open_confirmation(state, PIBUD_CONFIRM_FACTORY_RESET, false, 0, action);
        return;
    case PIBUD_SETTING_SOUND:
    case PIBUD_SETTING_TRANSCRIPT:
    default:
        pibud_copy(state->message, sizeof(state->message), "unavailable on this hardware");
        pibud_ui_refresh(action);
        break;
    }
}

void pibud_state_init(pibud_state_t *state, const pibud_settings_t *settings)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->connection = PIBUD_CONNECTION_OFFLINE;
    state->view = PIBUD_VIEW_HOME;
    state->heartbeat_stale = true;
    state->brightness_level = 4;
    if (settings != NULL) {
        state->settings = *settings;
        pibud_copy(state->name, sizeof(state->name), settings->name);
        pibud_copy(state->owner, sizeof(state->owner), settings->owner);
    }
    pibud_refresh_character(state, 0);
}

void pibud_state_reduce(pibud_state_t *state, const pibud_event_t *event,
                       uint64_t now_ms, pibud_action_t *action)
{
    if (action != NULL) {
        memset(action, 0, sizeof(*action));
    }
    if (state == NULL || event == NULL) {
        return;
    }

    pibud_clear_stale(state, now_ms);

    switch (event->type) {
    case PIBUD_EVENT_HEARTBEAT:
        pibud_apply_heartbeat(state, &event->heartbeat, event->ble.connection_generation,
                              now_ms, action);
        break;
    case PIBUD_EVENT_PROMPT:
        pibud_apply_prompt(state, &event->prompt, event->ble.connection_generation,
                           now_ms, action);
        break;
    case PIBUD_EVENT_TIME:
        state->epoch_seconds = event->time.epoch_seconds;
        state->tz_offset_seconds = event->time.tz_offset_seconds;
        state->time_received_ms = now_ms;
        pibud_ui_refresh(action);
        break;
    case PIBUD_EVENT_NAME:
        pibud_copy(state->name, sizeof(state->name), event->command.value);
        pibud_ui_refresh(action);
        break;
    case PIBUD_EVENT_OWNER:
        pibud_copy(state->owner, sizeof(state->owner), event->command.value);
        pibud_ui_refresh(action);
        break;
    case PIBUD_EVENT_STATUS_REQUEST:
        if (action != NULL) {
            action->type = PIBUD_ACTION_STATUS;
            action->connection_generation = event->ble.connection_generation;
        }
        break;
    case PIBUD_EVENT_UNPAIR_CONFIRMATION:
        if (state->confirmation == PIBUD_CONFIRM_NONE) {
            pibud_open_confirmation(state, PIBUD_CONFIRM_UNPAIR, true,
                                   event->ble.connection_generation, action);
        }
        break;
    case PIBUD_EVENT_BLE_CONNECTED:
        if (state->ble_generation != event->ble.connection_generation) {
            pibud_invalidate_prompt(state);
            state->passkey_visible = false;
            state->connected = false;
            state->heartbeat_stale = true;
            if (state->confirmation_acknowledge) {
                pibud_close_confirmation(state);
            }
        }
        state->ble_generation = event->ble.connection_generation;
        state->ble_connected = true;
        state->ble_encrypted = false;
        if (state->confirmation == PIBUD_CONFIRM_NONE) {
            state->connection = PIBUD_CONNECTION_PAIRING;
        }
        pibud_ui_refresh(action);
        break;
    case PIBUD_EVENT_BLE_DISCONNECTED:
        state->ble_generation = event->ble.connection_generation;
        state->ble_connected = false;
        state->ble_encrypted = false;
        state->passkey_visible = false;
        state->connected = false;
        state->heartbeat_stale = true;
        pibud_clear_session(state);
        if (state->confirmation_acknowledge) {
            pibud_close_confirmation(state);
        } else if (state->confirmation == PIBUD_CONFIRM_NONE) {
            state->connection = PIBUD_CONNECTION_OFFLINE;
        }
        pibud_ui_refresh(action);
        break;
    case PIBUD_EVENT_BLE_PASSKEY:
        if (event->ble.connection_generation != state->ble_generation) {
            break;
        }
        state->passkey = event->ble.passkey;
        state->passkey_visible = true;
        state->connection = PIBUD_CONNECTION_PAIRING;
        pibud_ui_refresh(action);
        break;
    case PIBUD_EVENT_BLE_ENCRYPTION:
        if (event->ble.connection_generation != state->ble_generation) {
            break;
        }
        state->ble_encrypted = event->ble.secure;
        if (event->ble.secure) {
            state->passkey_visible = false;
            if (state->confirmation == PIBUD_CONFIRM_NONE) {
                state->connection =
                    (state->connected && !state->heartbeat_stale)
                        ? PIBUD_CONNECTION_CONNECTED : PIBUD_CONNECTION_OFFLINE;
            }
        } else if (state->ble_connected && state->confirmation == PIBUD_CONFIRM_NONE) {
            state->connection = PIBUD_CONNECTION_PAIRING;
        }
        pibud_ui_refresh(action);
        break;
    case PIBUD_EVENT_BOND_DELETE_RESULT:
        pibud_copy(state->message, sizeof(state->message),
                   event->ble.success ? "unpaired" : "unpair failed");
        pibud_ui_refresh(action);
        break;
    case PIBUD_EVENT_ACT_RESULT:
        pibud_apply_act_result(state, &event->act_result, now_ms, action);
        break;
    case PIBUD_EVENT_KEY_CLICK:
        if (state->screen_off) {
            state->screen_off = false;
            if (action != NULL) {
                action->type = PIBUD_ACTION_DISPLAY_BACKLIGHT;
                action->brightness_percent =
                    (uint8_t)(20U + state->brightness_level * 20U);
            }
            break;
        }
        if (state->confirmation != PIBUD_CONFIRM_NONE && event->key == PIBUD_KEY_OK) {
            pibud_confirmation_t confirmation = state->confirmation;
            bool acknowledge = state->confirmation_acknowledge;
            uint32_t generation = state->confirmation_generation;

            pibud_close_confirmation(state);
            if (action != NULL) {
                action->type = (confirmation == PIBUD_CONFIRM_UNPAIR)
                                    ? PIBUD_ACTION_UNPAIR_CONFIRMED
                                    : PIBUD_ACTION_FACTORY_RESET_CONFIRMED;
                action->confirmation_acknowledge = acknowledge;
                action->connection_generation = generation;
            }
            break;
        }
        if (state->confirmation != PIBUD_CONFIRM_NONE && event->key == PIBUD_KEY_DOWN) {
            pibud_close_confirmation(state);
            pibud_ui_refresh(action);
            break;
        }
        if (state->confirmation != PIBUD_CONFIRM_NONE) {
            break;
        }
        if (pibud_has_actionable_prompt(state)) {
            if (event->key == PIBUD_KEY_OK) {
                pibud_decide_act(state, PIBUD_ACT_APPROVE, action);
            } else if (event->key == PIBUD_KEY_DOWN) {
                pibud_decide_act(state, PIBUD_ACT_DENY, action);
            } else if (event->key == PIBUD_KEY_UP) {
                if (action != NULL) {
                    action->type = PIBUD_ACTION_UI_SCROLL;
                    action->scroll_delta = -2 * PIBUD_SCROLL_STEP;
                }
            }
            break;
        }
        /* Normal navigation. */
        if (state->view == PIBUD_VIEW_MENU) {
            if (event->key == PIBUD_KEY_UP) {
                state->settings_sel = (pibud_setting_item_t)(
                    (state->settings_sel + PIBUD_SETTING_COUNT - 1U) % PIBUD_SETTING_COUNT);
                pibud_ui_refresh(action);
            } else if (event->key == PIBUD_KEY_DOWN) {
                state->settings_sel =
                    (pibud_setting_item_t)((state->settings_sel + 1U) % PIBUD_SETTING_COUNT);
                pibud_ui_refresh(action);
            } else if (event->key == PIBUD_KEY_OK) {
                pibud_apply_setting(state, action);
            }
            break;
        }
        if (event->key == PIBUD_KEY_UP) {
            if (action != NULL) {
                action->type = PIBUD_ACTION_UI_SCROLL;
                action->scroll_delta = -PIBUD_SCROLL_STEP;
            }
        } else if (event->key == PIBUD_KEY_DOWN) {
            if (action != NULL) {
                action->type = PIBUD_ACTION_UI_SCROLL;
                action->scroll_delta = PIBUD_SCROLL_STEP;
            }
        } else if (event->key == PIBUD_KEY_OK) {
            /* LIVE: jump to latest; other views: no-op refresh. */
            if (action != NULL) {
                action->type = (state->view == PIBUD_VIEW_LIVE) ? PIBUD_ACTION_UI_SCROLL
                                                                 : PIBUD_ACTION_UI_REFRESH;
                action->scroll_delta = (state->view == PIBUD_VIEW_LIVE) ? 0 : 0;
            }
        }
        break;
    case PIBUD_EVENT_KEY_LONG:
        if (event->key == PIBUD_KEY_UP && state->confirmation == PIBUD_CONFIRM_NONE) {
            pibud_view_cycle(state, action);
        } else if (event->key == PIBUD_KEY_DOWN &&
                   state->confirmation == PIBUD_CONFIRM_NONE) {
            if (action != NULL) {
                action->type = PIBUD_ACTION_UI_SCROLL;
                action->scroll_delta = 0; /* jump-to-latest / follow-live */
            }
        } else if (event->key == PIBUD_KEY_OK &&
                   state->confirmation == PIBUD_CONFIRM_NONE) {
            pibud_decide_act(state, PIBUD_ACT_INTERRUPT, action);
        }
        break;
    case PIBUD_EVENT_TICK:
        pibud_ui_refresh(action);
        break;
    case PIBUD_EVENT_NONE:
        break;
    }

    pibud_refresh_character(state, now_ms);
}

void pibud_state_snapshot(const pibud_state_t *state, pibud_ui_snapshot_t *snapshot)
{
    if (state == NULL || snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->connection = state->connection;
    snapshot->character = state->character;
    snapshot->view = state->view;
    snapshot->hb = state->hb;
    snapshot->prompt = state->prompt;
    snapshot->settings = state->settings;
    snapshot->scroll = state->scroll;
    snapshot->epoch_seconds = state->epoch_seconds;
    snapshot->tz_offset_seconds = state->tz_offset_seconds;
    snapshot->time_received_ms = state->time_received_ms;
    snapshot->approval_visible = pibud_has_actionable_prompt(state);
    snapshot->pairing_visible = state->passkey_visible;
    snapshot->confirmation_pending = state->confirmation_pending;
    snapshot->confirmation = state->confirmation;
    snapshot->settings_sel = state->settings_sel;
    snapshot->screen_off = state->screen_off;
    snapshot->brightness_level = state->brightness_level;
    snapshot->delivery = state->delivery;
    snapshot->approval_locked = state->approval_locked;
    snapshot->heartbeat_stale = state->heartbeat_stale;
    snapshot->ble_connected = state->ble_connected;
    snapshot->ble_encrypted = state->ble_encrypted;
    snapshot->battery_available = state->battery_available;
    snapshot->passkey_visible = state->passkey_visible;
    snapshot->passkey = state->passkey;
    snapshot->battery_percent = state->battery_percent;
    snapshot->battery_mv = state->battery_mv;
    pibud_copy(snapshot->name, sizeof(snapshot->name), state->name);
    pibud_copy(snapshot->owner, sizeof(snapshot->owner), state->owner);
    pibud_copy(snapshot->message, sizeof(snapshot->message), state->message);
}
