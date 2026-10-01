// main/pibud_ws.c — Wi-Fi data channel for Pi Agent Buddy.
//
// Untethered twin of pibud_usbc. On first boot the device runs a SoftAP with a
// small captive portal; the user points a phone browser at it, enters the Wi-Fi
// credentials once, and they are stored in NVS. Later boots connect straight to
// the station. Once the station holds an IP, a controller task discovers the
// sidecar's WebSocket server over mDNS (_pibuddy._tcp) and keeps an
// esp_websocket_client open:
//   - received text frames -> pibud_protocol_parse -> app event queue
//   - button acts          -> pibud_ws_send -> WebSocket text frame
//
// The portal is hand-rolled rather than wifi_provisioning: the espressif SoftAP
// scheme ships no web form, so configuring the device would need esp_prov.py on
// a computer, and joining the setup AP costs that computer its network.
#include "pibud_ws.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "esp_websocket_client.h"
#include "mdns.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "pibud_form.h"
#include "pibud_protocol.h"
#include "pibud_prov_html.h"
#include "pibud_types.h"

static const char *TAG = "pibud_ws";

#define PIBUD_MDNS_SERVICE    "_pibuddy"
#define PIBUD_MDNS_PROTO     "_tcp"
#define PIBUD_PROV_AP_SSID   "Pi-Buddy-Setup"
#define PIBUD_PROV_AP_IP     "192.168.4.1"
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
static httpd_handle_t s_prov_server;           // setup page, while unprovisioned
static bool s_ap_running;
static bool s_portal_busy;                     // a setup-portal task is in flight
static bool s_portal_resume_sta;               // the portal took over a retrying STA
static volatile bool s_sta_paused;             // radio handed to the setup portal
static unsigned s_connect_failures;            // consecutive join failures

// Attempts allowed after a save while the setup portal is open. A scanning
// station shares the radio with the access point, so retrying forever would
// degrade the very page the user is trying to load.
#define PIBUD_CONNECT_RETRIES 4

// After this many consecutive failures the setup portal reopens, so a mistyped
// password cannot lock the user out of the device (15 attempts is roughly 30 s,
// which is long enough not to fire on a transient router outage).
#define PIBUD_CONNECT_FALLBACK 15

// Credentials live in our own NVS namespace with the Wi-Fi driver's own NVS
// storage disabled, so there is exactly one source of truth and the mode can be
// chosen before esp_wifi_start().
#define PIBUD_NVS_NS     "pibud"
#define PIBUD_NVS_SSID   "wifi_ssid"
#define PIBUD_NVS_PASS   "wifi_pass"
#define PIBUD_SSID_MAX   32
#define PIBUD_PASS_MAX   64

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

// ---- SoftAP provisioning portal -------------------------------------------
//
// Served only while the device holds no credentials. Phones probe fixed URLs to
// detect a captive portal, so an unknown path answers with a redirect that makes
// the setup page open by itself.

static const char PROV_SAVED_PAGE[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Pi Agent Buddy</title></head>"
    "<body style=\"font-family:system-ui;margin:2rem;max-width:30rem\">"
    "<h2>Saved</h2><p>The device is joining your network. Retry here if the "
    "password was wrong: this page stays reachable until it succeeds.</p>"
    "</body></html>";

#define PIBUD_PROV_MAX_SSIDS 12
#define PIBUD_PROV_PAGE_MAX  2048

static char s_prov_ssids[PIBUD_PROV_MAX_SSIDS][PIBUD_SSID_MAX + 1];
static const char *s_prov_ssid_ptrs[PIBUD_PROV_MAX_SSIDS];
static size_t s_prov_ssid_count;
static char s_prov_page[PIBUD_PROV_PAGE_MAX];
static size_t s_prov_page_len;

// Collects the networks in range for the picker. Scanning while the setup AP is
// up is normal practice; it runs before any client can connect.
static void prov_scan_networks(void)
{
    s_prov_ssid_count = 0;

    if (esp_wifi_scan_start(NULL, true) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed; the page falls back to typing a name");
        return;
    }

    uint16_t found = 0;
    if (esp_wifi_scan_get_ap_num(&found) != ESP_OK || found == 0) {
        (void)esp_wifi_clear_ap_list(); // nothing will consume the results
        return;
    }

    wifi_ap_record_t *records = calloc(found, sizeof(*records));
    if (records == NULL) {
        (void)esp_wifi_clear_ap_list();
        return;
    }

    uint16_t got = found;
    if (esp_wifi_scan_get_ap_records(&got, records) != ESP_OK) {
        // Only a successful call releases the driver's cached results.
        (void)esp_wifi_clear_ap_list();
        free(records);
        return;
    }

    for (uint16_t i = 0; i < got && s_prov_ssid_count < PIBUD_PROV_MAX_SSIDS; i++) {
        const char *ssid = (const char *)records[i].ssid;
        if (ssid[0] == '\0' || strcmp(ssid, PIBUD_PROV_AP_SSID) == 0) {
            continue; // hidden network, or our own setup AP
        }
        strlcpy(s_prov_ssids[s_prov_ssid_count], ssid, PIBUD_SSID_MAX + 1);
        s_prov_ssid_ptrs[s_prov_ssid_count] = s_prov_ssids[s_prov_ssid_count];
        s_prov_ssid_count++;
    }
    free(records);

    ESP_LOGI(TAG, "setup page lists %u nearby network(s)", (unsigned)s_prov_ssid_count);
}

// Built once, after the scan, so every request is served from a fixed buffer.
// The picker is only a convenience: an entry for "not listed" plus a text field
// keeps hidden networks reachable.
static void prov_build_page(void)
{
    static const char prefix[] =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Pi Agent Buddy</title></head>"
        "<body style=\"font-family:system-ui;margin:2rem;max-width:30rem\">"
        "<h2>Pi Agent Buddy</h2>"
        "<p>Choose the Wi-Fi network this device should join.</p>"
        "<form method=\"post\" action=\"/save\">"
        "<p>Network<br><select name=\"ssid\" style=\"width:100%\">"
        "<option value=\"\">-- not listed / hidden --</option>";
    static const char suffix[] =
        "</select></p>"
        "<p>Only if it is not listed: network name<br>"
        "<input name=\"ssid_manual\" style=\"width:100%\" autocapitalize=\"off\""
        " autocorrect=\"off\" spellcheck=\"false\"></p>"
        "<p>Password<br><input name=\"pass\" type=\"password\" style=\"width:100%\"></p>"
        "<p><button type=\"submit\" style=\"padding:.6rem 1.2rem\">Save</button></p>"
        "</form></body></html>";

    int used = snprintf(s_prov_page, sizeof(s_prov_page), "%s", prefix);
    if (used < 0 || (size_t)used >= sizeof(s_prov_page)) {
        ESP_LOGE(TAG, "setup page prefix does not fit");
        return;
    }

    int options = pibud_prov_options_html(s_prov_ssid_ptrs, s_prov_ssid_count,
                                         s_prov_page + used,
                                         sizeof(s_prov_page) - (size_t)used);
    if (options < 0) {
        ESP_LOGW(TAG, "network list does not fit; the page lists nothing");
        options = 0;
        s_prov_page[used] = '\0';
    }
    used += options;

    int tail = snprintf(s_prov_page + used, sizeof(s_prov_page) - (size_t)used, "%s", suffix);
    if (tail < 0 || (size_t)tail >= sizeof(s_prov_page) - (size_t)used) {
        ESP_LOGE(TAG, "setup page does not fit");
        return;
    }
    s_prov_page_len = (size_t)used + (size_t)tail;
}


static bool prov_load_credentials(wifi_config_t *out)
{
    nvs_handle_t h;
    if (nvs_open(PIBUD_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    char ssid[PIBUD_SSID_MAX + 1] = {0};
    char pass[PIBUD_PASS_MAX + 1] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(pass);

    bool ok = nvs_get_str(h, PIBUD_NVS_SSID, ssid, &ssid_len) == ESP_OK && ssid[0] != '\0';
    if (ok) {
        // An absent password means an open network, not a read failure.
        if (nvs_get_str(h, PIBUD_NVS_PASS, pass, &pass_len) != ESP_OK) {
            pass[0] = '\0';
        }
        // A 32-character SSID fills the field exactly, so copy by length.
        memcpy(out->sta.ssid, ssid, strnlen(ssid, PIBUD_SSID_MAX));
        memcpy(out->sta.password, pass, strnlen(pass, PIBUD_PASS_MAX));
    }
    nvs_close(h);
    return ok;
}

static void prov_store_credentials(const wifi_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(PIBUD_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "cannot open NVS for credentials");
        return;
    }

    // Both fields are fixed-size and a maximum-length value has no terminator,
    // so nvs_set_str must not be handed the raw field.
    char ssid[PIBUD_SSID_MAX + 1] = {0};
    char pass[PIBUD_PASS_MAX + 1] = {0};
    memcpy(ssid, cfg->sta.ssid, PIBUD_SSID_MAX);
    memcpy(pass, cfg->sta.password, PIBUD_PASS_MAX);

    esp_err_t err = nvs_set_str(h, PIBUD_NVS_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, PIBUD_NVS_PASS, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storing credentials failed (%d)", err);
    }
}

static esp_err_t prov_get_handler(httpd_req_t *req)
{
    // Logged because the only view of this page is a phone browser; without a
    // request line, a failed load is indistinguishable from no request at all.
    ESP_LOGI(TAG, "setup page requested from %s", req->uri);
    if (s_prov_page_len == 0) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "page unavailable");
    }
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, s_prov_page, (ssize_t)s_prov_page_len);
}

static esp_err_t prov_save_handler(httpd_req_t *req)
{
    // httpd_req_recv() may return less than the whole body, so keep reading until
    // it is all in or the buffer is full.
    char body[192];
    size_t received = 0;
    while (received < sizeof(body) - 1) {
        int n = httpd_req_recv(req, body + received, sizeof(body) - 1 - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        received += (size_t)n;
    }
    body[received] = '\0';
    if (received == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
    }

    char ssid[PIBUD_SSID_MAX + 1] = {0};
    char manual[PIBUD_SSID_MAX + 1] = {0};
    char pass[PIBUD_PASS_MAX + 1] = {0};

    // The picker's "not listed" entry submits an empty ssid, so fall back to the
    // free-text field that hidden networks need.
    (void)pibud_form_get(body, received, "ssid", ssid, sizeof(ssid));
    if (ssid[0] == '\0') {
        (void)pibud_form_get(body, received, "ssid_manual", manual, sizeof(manual));
        memcpy(ssid, manual, sizeof(ssid));
    }
    if (ssid[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "network name required");
    }
    (void)pibud_form_get(body, received, "pass", pass, sizeof(pass));

    // Length only: the password itself must never reach the log.
    ESP_LOGI(TAG, "setup form: ssid \"%s\", password %u byte(s)", ssid,
             (unsigned)strnlen(pass, PIBUD_PASS_MAX));

    wifi_config_t wc = {0};
    memcpy(wc.sta.ssid, ssid, strnlen(ssid, PIBUD_SSID_MAX));
    memcpy(wc.sta.password, pass, strnlen(pass, PIBUD_PASS_MAX));

    // Answer before touching the radio: the phone must receive this page even
    // though a successful join tears the setup AP down moments later.
    httpd_resp_set_type(req, "text/html");
    (void)httpd_resp_send(req, PROV_SAVED_PAGE, HTTPD_RESP_USE_STRLEN);

    prov_store_credentials(&wc);
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) {
        ESP_LOGE(TAG, "applying Wi-Fi credentials failed");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "credentials saved; joining the configured network");
    s_connect_failures = 0; // give the corrected details a full set of attempts
    esp_wifi_connect();
    return ESP_OK;
}

static esp_err_t prov_not_found_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (err != HTTPD_404_NOT_FOUND) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "portal probe %s -> redirect", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" PIBUD_PROV_AP_IP "/");
    return httpd_resp_send(req, NULL, 0);
}

// Runs the setup portal. It scans for networks, which blocks for seconds and so
// must not happen in the Wi-Fi event loop, hence the one-shot task.
static void prov_portal_task(void *arg)
{
    (void)arg;

    if (s_portal_resume_sta) {
        // The driver refuses to scan while the station is connecting, and the
        // reconnect loop keeps it in exactly that state. Pausing the loop and
        // disconnecting first is what makes the picker work in the recovery
        // path, which is the case that matters most.
        s_sta_paused = true;
        (void)esp_wifi_disconnect();
    }

    // The AP netif owns the DHCP server, so it must exist before the access
    // point comes up or clients associate and never get a lease. Creating it here
    // covers both the first-run and the recovery path; doing it in the init path
    // only would leave the recovery portal without DHCP.
    esp_netif_create_default_wifi_ap();

    wifi_config_t ap = {0};
    memcpy(ap.ap.ssid, PIBUD_PROV_AP_SSID, sizeof(PIBUD_PROV_AP_SSID) - 1);
    ap.ap.ssid_len = sizeof(PIBUD_PROV_AP_SSID) - 1;
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    // Canonical order: mode, then config, then start.
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "setup access point failed; configure over USB instead");
        goto done;
    }
    s_ap_running = true;

    // Scan before serving, so the picker is ready and the page handler has
    // nothing to allocate or race over.
    prov_scan_networks();
    prov_build_page();

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.stack_size = 4096;
    hcfg.max_uri_handlers = 3;
    hcfg.lru_purge_enable = true; // browsers keep connections open
    if (httpd_start(&s_prov_server, &hcfg) != ESP_OK) {
        ESP_LOGE(TAG, "setup page failed to start");
        s_prov_server = NULL;
        goto done;
    }
    httpd_uri_t get_uri = { .uri = "/", .method = HTTP_GET, .handler = prov_get_handler };
    httpd_uri_t save_uri = { .uri = "/save", .method = HTTP_POST, .handler = prov_save_handler };
    (void)httpd_register_uri_handler(s_prov_server, &get_uri);
    (void)httpd_register_uri_handler(s_prov_server, &save_uri);
    (void)httpd_register_err_handler(s_prov_server, HTTPD_404_NOT_FOUND, prov_not_found_handler);

    ESP_LOGI(TAG, "setup portal up: join \"%s\" and open http://%s/",
             PIBUD_PROV_AP_SSID, PIBUD_PROV_AP_IP);

    if (s_portal_resume_sta) {
        s_sta_paused = false;
        esp_wifi_connect(); // keep trying, in case the network comes back
    }

done:
    s_sta_paused = false;
    s_portal_busy = false;
    vTaskDelete(NULL);
}

static void prov_portal_start(bool resume_sta)
{
    if (s_portal_busy || s_prov_server != NULL) {
        return;
    }
    s_portal_resume_sta = resume_sta;
    s_portal_busy = true;
    if (xTaskCreate(prov_portal_task, "pibud_prov", 4096, NULL,
                    PIBUD_WS_CTRL_PRIO, NULL) != pdPASS) {
        s_portal_busy = false;
        ESP_LOGE(TAG, "setup portal task failed; configure over USB instead");
    }
}

// The station is online, so the setup AP has done its job: stopping it frees the
// radio and closes an open access point. This runs from the got-IP handler,
// which is a different task from the HTTP server, so httpd_stop() cannot
// deadlock against the request being served.
static void prov_portal_stop(void)
{
    if (s_prov_server != NULL) {
        (void)httpd_stop(s_prov_server);
        s_prov_server = NULL;
    }
    if (s_ap_running) {
        s_ap_running = false;
        s_sta_paused = false; // the station owns the radio again
        if (esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK) {
            ESP_LOGI(TAG, "setup access point closed");
        }
    }
}

// ---- Wi-Fi event handlers -------------------------------------------------
static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)id;
    (void)data;
    prov_portal_stop();
    s_connect_failures = 0;
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
    const wifi_event_sta_disconnected_t *d = (const wifi_event_sta_disconnected_t *)data;

    if (s_sta_paused) {
        return; // the setup portal has the radio; its task restarts the attempts
    }

    s_connect_failures++;
    if (s_connect_failures == 1 || s_connect_failures % 5 == 0) {
        // The reason code separates a wrong password from an out-of-range AP;
        // the SSID is not a secret, the password never appears here.
        ESP_LOGW(TAG, "cannot join \"%s\" (reason %u, attempt %u); retrying",
                 (d != NULL) ? (const char *)d->ssid : "?",
                 (d != NULL) ? (unsigned)d->reason : 0u, s_connect_failures);
    }

    // A mistyped password must not lock the user out of the device, so once the
    // failures stop looking transient the setup portal comes back.
    if (s_connect_failures == PIBUD_CONNECT_FALLBACK && s_prov_server == NULL) {
        ESP_LOGW(TAG, "still offline after %u attempts; reopening the setup portal",
                 s_connect_failures);
        // Restart the accounting with the portal up, so the retry allowance below
        // applies from here rather than being already spent.
        s_connect_failures = 0;
        prov_portal_start(true);
        esp_wifi_connect();
        return;
    }

    // While the portal is open, stop hammering a network we already failed to
    // join: each attempt scans, and a scanning station shares the radio with the
    // access point. The POST handler resets this count, so a corrected password
    // gets fresh attempts.
    if (s_prov_server != NULL && s_connect_failures >= PIBUD_CONNECT_RETRIES) {
        if (s_connect_failures == PIBUD_CONNECT_RETRIES) {
            ESP_LOGW(TAG, "retries paused while the setup portal is open; "
                          "correct the details at http://%s/", PIBUD_PROV_AP_IP);
        }
        return;
    }

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

    // The Wi-Fi mode is chosen before esp_wifi_start(), so the station and the
    // setup access point never fight over the radio or the default netif.
    wifi_config_t stored = {0};
    if (prov_load_credentials(&stored)) {
        ESP_LOGI(TAG, "stored credentials found; connecting to the saved network");
        if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
            esp_wifi_set_config(WIFI_IF_STA, &stored) != ESP_OK ||
            esp_wifi_start() != ESP_OK) {
            ESP_LOGW(TAG, "STA bring-up failed");
            return ESP_FAIL;
        }
        esp_wifi_connect();
        return ESP_OK;
    }

    // The AP netif is created by the portal itself, which also runs when the
    // setup portal reopens after failed joins.
    prov_portal_start(false);
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
