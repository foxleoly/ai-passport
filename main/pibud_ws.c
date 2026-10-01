// main/pibud_ws.c — Wi-Fi data channel for Pi Agent Buddy.
//
// Untethered twin of pibud_usbc. On first boot a SoftAP provisioning access
// point (temporary SSID) lets the user configure their Wi-Fi once (the
// espressif wifi_provisioning component stores credentials in NVS); later
// boots auto-connect the STA. Once the STA holds an IP, a controller task
// discovers the sidecar's WebSocket server via mDNS (_pibuddy._tcp) and keeps
// an esp_websocket_client open:
//   - received text frames -> pibud_protocol_parse -> app event queue
//   - button acts          -> pibud_ws_send -> WebSocket text frame
#include "pibud_ws.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "esp_websocket_client.h"
#include "mdns.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

#include "pibud_protocol.h"
#include "pibud_types.h"

static const char *TAG = "pibud_ws";

#define PIBUD_MDNS_SERVICE    "_pibuddy"
#define PIBUD_MDNS_PROTO     "_tcp"
#define PIBUD_PROV_AP_SSID   "Pi-Buddy-Setup"
#define PIBUD_WS_RX_SIZE     2048
#define PIBUD_WS_CTRL_STACK  4096
#define PIBUD_WS_CTRL_PRIO   3
#define PIBUD_WS_SCAN_MS     3000
#define PIBUD_MDNS_QUERY_MS  2000

typedef struct {
    uint32_t ip;
    uint16_t port;
} ws_target_t;

static QueueHandle_t s_app_queue;              // pibud app event queue
static esp_websocket_client_handle_t s_client;
static ws_target_t s_target;                   // target the client is bound to
static volatile bool s_connected;
static char s_rx[PIBUD_WS_RX_SIZE];            // assembled WS text payload
static bool s_ctrl_started;
static esp_event_handler_instance_t s_got_ip_hdl;
static esp_event_handler_instance_t s_disc_hdl;

static void ws_client_ensure(const ws_target_t *t);

// ---- WebSocket event handler ---------------------------------------------
static void ws_event_cb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_websocket_event_data_t *d = (const esp_websocket_event_data_t *)data;

    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "websocket connected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (d->op_code != 0x01 && d->op_code != 0x02) { // text / binary only
            break;
        }
        {
            size_t off = (size_t)d->payload_offset;
            size_t n = (size_t)d->data_len;
            if (off + n >= sizeof(s_rx)) {
                ESP_LOGW(TAG, "ws frame too large (%u); dropped", (unsigned)(off + n));
                break;
            }
            memcpy(s_rx + off, d->data_ptr, n);
            if (d->fin) {
                s_rx[off + n] = '\0';
                pibud_event_t ev;
                memset(&ev, 0, sizeof(ev));
                if (pibud_protocol_parse(s_rx, &ev) == PIBUD_PROTO_OK) {
                    ev.ble.connection_generation = 0; // WS transport
                    (void)xQueueSend(s_app_queue, &ev, 0);
                }
            }
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_ERROR:
    case WEBSOCKET_EVENT_CLOSED:
        s_connected = false;
        break;
    default:
        break;
    }
}

// ---- mDNS discovery + WebSocket lifecycle --------------------------------
static void ws_client_ensure(const ws_target_t *t)
{
    if (s_client != NULL && s_target.ip == t->ip && s_target.port == t->port) {
        return; // already bound to this target
    }
    s_target = *t;

    static char host[16];
    // esp_ip4_addr_t.addr is network byte order; esp_ip4addr_ntoa formats it
    // correctly. Shifting the uint32 by hand byte-reverses the address.
    esp_ip4_addr_t addr = { .addr = t->ip };
    esp_ip4addr_ntoa(&addr, host, sizeof(host));

    if (s_client != NULL) {
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
        s_connected = false;
    }

    esp_websocket_client_config_t cfg = {0};
    cfg.host = host; // static; valid for the client's lifetime
    cfg.port = t->port;
    cfg.path = "/";
    cfg.transport = WEBSOCKET_TRANSPORT_OVER_TCP;
    cfg.buffer_size = PIBUD_WS_RX_SIZE;
    cfg.task_stack = PIBUD_WS_CTRL_STACK;
    cfg.task_prio = PIBUD_WS_CTRL_PRIO;
    cfg.reconnect_timeout_ms = 5000; // built-in auto-reconnect

    s_client = esp_websocket_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "websocket client init failed (%s:%u)", host, (unsigned)t->port);
        return;
    }
    (void)esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, ws_event_cb, NULL);
    if (esp_websocket_client_start(s_client) != ESP_OK) {
        ESP_LOGW(TAG, "websocket client start failed");
    }
    ESP_LOGI(TAG, "websocket target %s:%u", host, (unsigned)t->port);
}

static void discover_sidecar(void)
{
    mdns_result_t *res = NULL;
    if (mdns_query_ptr(PIBUD_MDNS_SERVICE, PIBUD_MDNS_PROTO,
                       PIBUD_MDNS_QUERY_MS, 1, &res) != ESP_OK || res == NULL) {
        return; // sidecar not advertised yet
    }
    char instance[MDNS_NAME_BUF_LEN];
    strncpy(instance, res->instance_name, sizeof(instance) - 1);
    instance[sizeof(instance) - 1] = '\0';
    mdns_query_results_free(res);

    mdns_result_t *srv = NULL;
    if (mdns_query_srv(instance, PIBUD_MDNS_SERVICE, PIBUD_MDNS_PROTO,
                       PIBUD_MDNS_QUERY_MS, &srv) != ESP_OK || srv == NULL) {
        return;
    }
    char host[MDNS_NAME_BUF_LEN];
    strncpy(host, srv->hostname, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    uint16_t port = srv->port;
    mdns_query_results_free(srv);

    esp_ip4_addr_t ip4 = {0};
    if (mdns_query_a(host, PIBUD_MDNS_QUERY_MS, &ip4) != ESP_OK || ip4.addr == 0) {
        return;
    }
    ws_target_t t = { .ip = ip4.addr, .port = port };
    ws_client_ensure(&t);
}

static void ws_ctrl_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000)); // let Wi-Fi / mDNS settle
    for (;;) {
        discover_sidecar();
        vTaskDelay(pdMS_TO_TICKS(PIBUD_WS_SCAN_MS));
    }
}

// ---- Wi-Fi event handlers -------------------------------------------------
static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    if (s_ctrl_started) {
        return;
    }
    s_ctrl_started = true;
    ESP_LOGI(TAG, "STA got IP; starting mDNS + WebSocket discovery");
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mdns_init failed");
        return;
    }
    if (xTaskCreate(ws_ctrl_task, "pibud_ws_ctrl", PIBUD_WS_CTRL_STACK,
                    NULL, PIBUD_WS_CTRL_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "ws ctrl task create failed");
    }
}

static void on_sta_disconnected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    ESP_LOGI(TAG, "Wi-Fi disconnected; reconnecting");
    esp_wifi_connect();
}

// ---- public API -----------------------------------------------------------
esp_err_t pibud_ws_init(QueueHandle_t event_queue)
{
    s_app_queue = event_queue;

    // The Wi-Fi netif helpers below register their handlers on the default
    // event loop, so both stack and loop must exist first (ESP_ERR_INVALID_STATE
    // here aborts inside ESP_ERROR_CHECK and boot-loops the device).
    esp_netif_init();
    esp_event_loop_create_default();

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap(); // SoftAP scheme reuses this netif

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&wcfg) != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_init failed; Wi-Fi data channel unavailable");
        return ESP_FAIL;
    }
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            on_got_ip, NULL, &s_got_ip_hdl) != ESP_OK ||
        esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                            on_sta_disconnected, NULL, &s_disc_hdl) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi event handler register failed");
    }
    // NOTE: the station is deliberately NOT started here. wifi_prov_mgr_init()
    // only needs esp_wifi_init(); the provisioning manager sets the Wi-Fi mode
    // and calls esp_wifi_start() itself. Starting beforehand with WIFI_MODE_NULL
    // leaves the station down, so the already-provisioned path below has to
    // bring it up explicitly before esp_wifi_connect().
    wifi_prov_mgr_config_t pcfg = {0};
    pcfg.scheme = wifi_prov_scheme_softap;
    if (wifi_prov_mgr_init(pcfg) != ESP_OK) {
        ESP_LOGW(TAG, "wifi_prov_mgr_init failed; provisioning unavailable");
    }

    bool provisioned = false;
    if (wifi_prov_mgr_is_provisioned(&provisioned) == ESP_OK && provisioned) {
        ESP_LOGI(TAG, "provisioned; connecting to stored Wi-Fi");
        // esp_wifi_start() reloads the STA credentials saved in NVS.
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_start() != ESP_OK) {
            ESP_LOGW(TAG, "STA bring-up failed");
        } else {
            esp_wifi_connect();
        }
    } else {
        ESP_LOGI(TAG, "not provisioned; SoftAP provisioning on %s", PIBUD_PROV_AP_SSID);
        // SECURITY_0 = plain (no protocomm encryption); local first-run only.
        wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_0, NULL,
                                         PIBUD_PROV_AP_SSID, NULL);
    }
    return ESP_OK;
}

esp_err_t pibud_ws_send(const char *data, size_t length)
{
    if (s_client == NULL || !s_connected || data == NULL) {
        return ESP_FAIL;
    }
    int sent = esp_websocket_client_send_text(s_client, data, (int)length, 0);
    return sent == (int)length ? ESP_OK : ESP_FAIL;
}

bool pibud_ws_is_connected(void)
{
    return s_connected;
}
