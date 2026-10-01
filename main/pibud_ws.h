// main/pibud_ws.h — WiFi data channel: SoftAP provisioning + mDNS discovery
// + WebSocket client to the sidecar. The twin of pibud_usbc (tethered) that
// works untethered: the device joins the user's Wi-Fi, finds the Mac's
// WebSocket server via mDNS (_pibuddy._tcp), and moves heartbeat/act JSON over
// the socket.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* Start the WiFi data channel:
 *   - first boot (no stored credentials): bring up the SoftAP provisioning
 *     access point + built-in web config page so the user can enter their
 *     home Wi-Fi once; credentials persist in NVS.
 *   - later boots: auto-connect STA to the stored network.
 * Once the STA holds an IP, the module discovers the sidecar via mDNS and
 * opens a WebSocket, feeding received frames to pibud_protocol_parse and
 * posting pibud_event_t onto event_queue (same as pibud_usbc).
 * Non-fatal: failures log and the device keeps running (BLE/USB still work).
 */
esp_err_t pibud_ws_init(QueueHandle_t event_queue);

/* Send one act JSON (text frame) to the connected sidecar. ESP_FAIL when the
 * WebSocket is not currently connected. */
esp_err_t pibud_ws_send(const char *data, size_t length);

bool pibud_ws_is_connected(void);
