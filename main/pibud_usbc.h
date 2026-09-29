#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* App-level JSON-line transport over USB-Serial-JTAG (VFS /dev/usbserjtag),
 * the same wire the console/flash port uses. The Mac sidecar opens this port
 * and: writes heartbeat JSON lines (device consumes via pibud_protocol_parse),
 * reads act JSON lines the device emits on button presses. This is the
 * tethered (USB) twin of the BLE NUS transport; it feeds the same app event
 * queue, so the state machine / UI are transport-agnostic.
 *
 * Console log lines share the TX wire; the sidecar ignores anything that is
 * not a protocol JSON line, so interleave is harmless. */

/* Open the data channel and start the RX task. Feeds parsed protocol events
 * into event_queue (the pibud app queue). */
esp_err_t pibud_usbc_init(QueueHandle_t event_queue);

/* Write one act JSON line (including its trailing newline) to the Mac.
 * ESP_OK on full write; ESP_FAIL if the port is not open. */
esp_err_t pibud_usbc_send(const char *data, size_t length);

bool pibud_usbc_is_open(void);
