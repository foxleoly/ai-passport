// main/pibud_app.c — Pi Agent Buddy application entry + task wiring.
//
// Data flow:
//   BLE NUS RX line -> pibud_protocol_parse -> pibud_event_t -> queue -> worker
//   button (UP/DN/OK click/long) -> pibud_event_t -> queue -> worker
//   worker: pibud_state_reduce -> dispatch (UI render under bsp_lvgl_lock,
//           BLE TX for acts, backlight, bond deletion)
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"

#include "pibud_ble.h"
#include "pibud_protocol.h"
#include "pibud_state.h"
#include "pibud_ui.h"
#include "pibud_usbc.h"

static const char *TAG = "pibud_app";

#define APP_QUEUE_DEPTH 16
#define TICK_PERIOD_MS 200

static pibud_state_t s_state;
static QueueHandle_t s_queue;
static pibud_settings_t s_settings;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static void dispatch_action(const pibud_action_t *action)
{
    pibud_ui_snapshot_t snap;

    switch (action->type) {
    case PIBUD_ACTION_ACT:
        {
            char buf[256];
            int n = pibud_protocol_act_json(buf, sizeof(buf), action->act.kind,
                                            action->act.id, action->act.tool);
            if (n > 0) {
                buf[n] = '\n';
                buf[n + 1] = '\0';
                if (pibud_ble_send(buf, (size_t)n + 1) != ESP_OK) {
                    ESP_LOGW(TAG, "act send failed");
                }
                // Also emit over the tethered USB channel when it is open.
                if (pibud_usbc_is_open()) {
                    if (pibud_usbc_send(buf, (size_t)n + 1) != ESP_OK) {
                        ESP_LOGW(TAG, "usbc act send failed");
                    }
                }
            }
            break;
        }
    case PIBUD_ACTION_BLE_TOGGLE:
        if (action->ble_enabled) {
            pibud_ble_start();
        } else {
            pibud_ble_stop();
        }
        break;
    case PIBUD_ACTION_UNPAIR_CONFIRMED:
        pibud_ble_delete_bonds();
        break;
    case PIBUD_ACTION_DISPLAY_BACKLIGHT:
        bsp_display_backlight(action->brightness_percent);
        break;
    case PIBUD_ACTION_SCREEN_OFF:
        bsp_display_backlight(0);
        break;
    case PIBUD_ACTION_UI_REFRESH:
    case PIBUD_ACTION_SETTINGS:
    case PIBUD_ACTION_STATUS:
    default:
        break;
    }

    // Refresh the UI for any action (and after state changes).
    int soc = bsp_battery_soc();
    s_state.battery_available = soc >= 0;
    s_state.battery_percent = (uint8_t)(soc > 0 ? soc : 0);
    s_state.battery_mv = (uint16_t)bsp_battery_mv();
    if (bsp_lvgl_lock(300)) {
        pibud_state_snapshot(&s_state, &snap);
        pibud_ui_render(&snap);
        bsp_lvgl_unlock();
    }
}

static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    pibud_key_t key;
    pibud_event_type_t type;

    switch (btn) {
    case BSP_BTN_UP: key = PIBUD_KEY_UP; break;
    case BSP_BTN_DOWN: key = PIBUD_KEY_DOWN; break;
    default: key = PIBUD_KEY_OK; break;
    }
    if (ev == BSP_BTN_CLICK) {
        type = PIBUD_EVENT_KEY_CLICK;
    } else if (ev == BSP_BTN_LONG) {
        type = PIBUD_EVENT_KEY_LONG;
    } else {
        return; // PRESS/DOUBLE unused for MVP
    }
    pibud_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.key = key;
    (void)xQueueSend(s_queue, &e, 0);
}

static void on_ble_event(const pibud_ble_event_t *e, void *user)
{
    (void)user;
    pibud_event_t ev;
    memset(&ev, 0, sizeof(ev));

    switch (e->type) {
    case PIBUD_BLE_EVENT_CONNECTED:
        ev.type = PIBUD_EVENT_BLE_CONNECTED;
        ev.ble.connection_generation = e->data.connected.connection_generation;
        break;
    case PIBUD_BLE_EVENT_DISCONNECTED:
        ev.type = PIBUD_EVENT_BLE_DISCONNECTED;
        ev.ble.connection_generation = e->data.disconnected.connection_generation;
        break;
    case PIBUD_BLE_EVENT_PASSKEY:
        ev.type = PIBUD_EVENT_BLE_PASSKEY;
        ev.ble.passkey = e->data.passkey.value;
        ev.ble.connection_generation = e->data.passkey.connection_generation;
        break;
    case PIBUD_BLE_EVENT_ENCRYPTION:
        ev.type = PIBUD_EVENT_BLE_ENCRYPTION;
        ev.ble.secure = e->data.encryption.encrypted;
        ev.ble.connection_generation = e->data.encryption.connection_generation;
        break;
    case PIBUD_BLE_EVENT_BOND_DELETE_RESULT:
        ev.type = PIBUD_EVENT_BOND_DELETE_RESULT;
        ev.ble.success = e->data.bond_delete_result.success;
        break;
    case PIBUD_BLE_EVENT_RX_LINE: {
        int rc = pibud_protocol_parse(e->data.rx_line.data, &ev);
        if (rc != PIBUD_PROTO_OK) {
            if (rc == PIBUD_PROTO_UNKNOWN) {
                ESP_LOGD(TAG, "unknown protocol cmd");
            }
            return;
        }
        ev.ble.connection_generation = e->data.rx_line.connection_generation;
        break;
    }
    default:
        return;
    }
    (void)xQueueSend(s_queue, &ev, 0);
}

static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        pibud_event_t ev;
        pibud_action_t action;
        TickType_t wait = TICK_PERIOD_MS / portTICK_PERIOD_MS;

        if (xQueueReceive(s_queue, &ev, wait) == pdTRUE) {
            pibud_state_reduce(&s_state, &ev, now_ms(), &action);
        } else {
            pibud_event_t tick = {0};
            tick.type = PIBUD_EVENT_TICK;
            pibud_state_reduce(&s_state, &tick, now_ms(), &action);
        }
        dispatch_action(&action);
    }
}

void pibud_app_start(void)
{
    // BLE (NimBLE) persists bonding/EAD in NVS; init before pibud_ble_init.
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init returned %d; BLE bonding persistence degraded", err);
    }

    memset(&s_settings, 0, sizeof(s_settings));
    pibud_state_init(&s_state, &s_settings);

    s_queue = xQueueCreate(APP_QUEUE_DEPTH, sizeof(pibud_event_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "app queue alloc failed");
        return;
    }

    pibud_ble_config_t ble_cfg;
    memset(&ble_cfg, 0, sizeof(ble_cfg));
    ble_cfg.event_cb = on_ble_event;
    ble_cfg.event_context = NULL;
    if (pibud_ble_init(&ble_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "BLE init failed; running without BLE");
    }

    if (xTaskCreate(worker_task, "pibud_worker", 8192, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "worker task create failed");
        return;
    }
    pibud_ble_start();
    // Tethered USB data channel (heartbeat in, act out) alongside BLE.
    if (pibud_usbc_init(s_queue) != ESP_OK) {
        ESP_LOGW(TAG, "USB data channel unavailable; BLE only");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Pi Agent Buddy starting");

    bsp_i2c_init();
    bsp_i2c_scan();

    if (bsp_display_init() != ESP_OK || bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "display/LVGL init failed; cannot continue");
        return;
    }
    bsp_display_backlight(100);
    bsp_battery_init();
    (void)bsp_audio_init();

    if (bsp_lvgl_lock(1000)) {
        pibud_ui_init();
        bsp_lvgl_unlock();
    }

    if (bsp_button_init(on_button, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
        return;
    }

    pibud_app_start();
    ESP_LOGI(TAG, "ready");
}
