#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "pibud_state.h"
#include "pibud_types.h"

static pibud_event_t ev(pibud_event_type_t type, pibud_key_t key)
{
    pibud_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.key = key;
    e.ble.connection_generation = 1;
    return e;
}

int main(void)
{
    pibud_settings_t settings;
    pibud_state_t s;
    pibud_action_t a;
    pibud_event_t e;
    pibud_ui_snapshot_t snap;
    uint64_t now = 1000;

    memset(&settings, 0, sizeof(settings));
    settings.brightness_level = PIBUD_BRIGHTNESS_UNSET;
    pibud_state_init(&s, &settings);

    /* init defaults */
    assert(s.view == PIBUD_VIEW_HOME);
    assert(s.connection == PIBUD_CONNECTION_OFFLINE);
    assert(s.character == PIBUD_CHAR_SLEEP);
    assert(s.heartbeat_stale);
    assert(s.brightness_level == 4); /* full brightness by default */

    /* heartbeat connected+running -> BUSY, CONNECTED */
    e = ev(PIBUD_EVENT_HEARTBEAT, PIBUD_KEY_NONE);
    e.heartbeat.connected = true;
    e.heartbeat.state[0] = 'r'; strcpy(e.heartbeat.state, "running");
    e.heartbeat.sub_working = 1;
    pibud_state_reduce(&s, &e, now, &a);
    assert(s.connected);
    assert(s.connection == PIBUD_CONNECTION_CONNECTED);
    assert(s.character == PIBUD_CHAR_BUSY);
    assert(a.type == PIBUD_ACTION_UI_REFRESH);

    /* prompt -> approval overlay + ATTENTION */
    e = ev(PIBUD_EVENT_PROMPT, PIBUD_KEY_NONE);
    e.prompt.connected = true;
    e.prompt.id_length = 3;
    strcpy(e.prompt.id, "p1");
    e.prompt.id_length = 2;
    strcpy(e.prompt.text, "flash fw?");
    pibud_state_reduce(&s, &e, now + 10, &a);
    pibud_state_snapshot(&s, &snap);
    assert(snap.approval_visible);
    assert(snap.character == PIBUD_CHAR_ATTENTION);

    /* OK short in approval -> ACT APPROVE, locks */
    e = ev(PIBUD_EVENT_KEY_CLICK, PIBUD_KEY_OK);
    pibud_state_reduce(&s, &e, now + 20, &a);
    assert(a.type == PIBUD_ACTION_ACT);
    assert(a.act.kind == PIBUD_ACT_APPROVE);
    assert(s.approval_locked);
    assert(s.delivery == PIBUD_DELIVERY_SENDING);

    /* re-click while locked -> no act */
    pibud_state_reduce(&s, &e, now + 30, &a);
    assert(a.type == PIBUD_ACTION_NONE || a.type == PIBUD_ACTION_UI_REFRESH);

    /* herdr "blocked" mirrors into an approval prompt; unblocking clears it */
    pibud_state_init(&s, &settings);
    e = ev(PIBUD_EVENT_HEARTBEAT, PIBUD_KEY_NONE);
    e.heartbeat.connected = true;
    strcpy(e.heartbeat.state, "blocked");
    pibud_state_reduce(&s, &e, now, &a);
    pibud_state_snapshot(&s, &snap);
    assert(snap.approval_visible);
    e = ev(PIBUD_EVENT_KEY_CLICK, PIBUD_KEY_OK);
    pibud_state_reduce(&s, &e, now + 10, &a);
    assert(a.type == PIBUD_ACTION_ACT);
    assert(a.act.kind == PIBUD_ACT_APPROVE);
    assert(s.approval_locked);
    /* The agent stops waiting -> the prompt is cleared and the attempt forgotten,
     * so a later blocked episode can be answered too. */
    e = ev(PIBUD_EVENT_HEARTBEAT, PIBUD_KEY_NONE);
    e.heartbeat.connected = true;
    strcpy(e.heartbeat.state, "idle");
    pibud_state_reduce(&s, &e, now + 20, &a);
    assert(s.prompt.id[0] == '\0');
    assert(!s.approval_locked);

    /* interrupt: OK long with no actionable prompt -> ACT INTERRUPT */
    pibud_state_init(&s, &settings);
    e = ev(PIBUD_EVENT_KEY_LONG, PIBUD_KEY_OK);
    pibud_state_reduce(&s, &e, now, &a);
    assert(a.type == PIBUD_ACTION_ACT);
    assert(a.act.kind == PIBUD_ACT_INTERRUPT);

    /* UP long cycles views HOME->LIVE->STATS->MENU->HOME */
    pibud_state_init(&s, &settings);
    assert(s.view == PIBUD_VIEW_HOME);
    e = ev(PIBUD_EVENT_KEY_LONG, PIBUD_KEY_UP);
    pibud_state_reduce(&s, &e, now, &a);
    assert(s.view == PIBUD_VIEW_LIVE);
    pibud_state_reduce(&s, &e, now, &a);
    assert(s.view == PIBUD_VIEW_STATS);
    pibud_state_reduce(&s, &e, now, &a);
    assert(s.view == PIBUD_VIEW_MENU);
    pibud_state_reduce(&s, &e, now, &a);
    assert(s.view == PIBUD_VIEW_HOME);

    /* DOWN long on LIVE -> scroll-to-latest action */
    pibud_state_init(&s, &settings);
    s.view = PIBUD_VIEW_LIVE;
    e = ev(PIBUD_EVENT_KEY_LONG, PIBUD_KEY_DOWN);
    pibud_state_reduce(&s, &e, now, &a);
    assert(a.type == PIBUD_ACTION_UI_SCROLL);

    /* menu: move selection + apply */
    pibud_state_init(&s, &settings);
    s.view = PIBUD_VIEW_MENU;
    e = ev(PIBUD_EVENT_KEY_CLICK, PIBUD_KEY_DOWN);
    pibud_state_reduce(&s, &e, now, &a);
    assert(s.settings_sel == PIBUD_SETTING_FACTORY_RESET);
    e = ev(PIBUD_EVENT_KEY_CLICK, PIBUD_KEY_UP);
    pibud_state_reduce(&s, &e, now, &a);
    assert(s.settings_sel == PIBUD_SETTING_BRIGHTNESS);
    e = ev(PIBUD_EVENT_KEY_CLICK, PIBUD_KEY_OK);
    pibud_state_reduce(&s, &e, now, &a);
    assert(a.type == PIBUD_ACTION_DISPLAY_BACKLIGHT);

    /* persisted brightness is honored; out-of-range falls back to default */
    settings.brightness_level = 2;
    pibud_state_init(&s, &settings);
    assert(s.brightness_level == 2);
    assert(s.settings.brightness_level == 2);
    settings.brightness_level = PIBUD_BRIGHTNESS_STEPS; /* out of range */
    pibud_state_init(&s, &settings);
    assert(s.brightness_level == 4);
    settings.brightness_level = PIBUD_BRIGHTNESS_UNSET; /* back to default */
    pibud_state_init(&s, &settings);
    assert(s.brightness_level == 4);

    /* factory reset: MENU OK opens a confirmation, OK again confirms */
    pibud_state_init(&s, &settings);
    s.view = PIBUD_VIEW_MENU;
    s.settings_sel = PIBUD_SETTING_FACTORY_RESET;
    e = ev(PIBUD_EVENT_KEY_CLICK, PIBUD_KEY_OK);
    pibud_state_reduce(&s, &e, now, &a);
    assert(s.confirmation == PIBUD_CONFIRM_FACTORY_RESET);
    e = ev(PIBUD_EVENT_KEY_CLICK, PIBUD_KEY_OK);
    pibud_state_reduce(&s, &e, now + 5, &a);
    assert(a.type == PIBUD_ACTION_FACTORY_RESET_CONFIRMED);

    /* unpair confirmation flow */
    pibud_state_init(&s, &settings);
    e = ev(PIBUD_EVENT_UNPAIR_CONFIRMATION, PIBUD_KEY_NONE);
    pibud_state_reduce(&s, &e, now, &a);
    pibud_state_snapshot(&s, &snap);
    assert(snap.confirmation_pending);
    assert(snap.connection == PIBUD_CONNECTION_CONFIRMING);
    e = ev(PIBUD_EVENT_KEY_CLICK, PIBUD_KEY_OK);
    pibud_state_reduce(&s, &e, now + 5, &a);
    assert(a.type == PIBUD_ACTION_UNPAIR_CONFIRMED);

    /* heartbeat timeout -> stale + session cleared */
    pibud_state_init(&s, &settings);
    e = ev(PIBUD_EVENT_HEARTBEAT, PIBUD_KEY_NONE);
    e.heartbeat.connected = true;
    strcpy(e.heartbeat.state, "running");
    pibud_state_reduce(&s, &e, 1000, &a);
    assert(s.connected);
    e = ev(PIBUD_EVENT_TICK, PIBUD_KEY_NONE);
    pibud_state_reduce(&s, &e, 1000 + 30000 + 1, &a); /* past 30s timeout */
    assert(s.heartbeat_stale);
    assert(!s.connected);

    /* disconnect clears session */
    pibud_state_init(&s, &settings);
    e = ev(PIBUD_EVENT_BLE_DISCONNECTED, PIBUD_KEY_NONE);
    pibud_state_reduce(&s, &e, now, &a);
    assert(s.connection == PIBUD_CONNECTION_OFFLINE);
    assert(s.ble_connected == false);

    return 0;
}
