// main/pibud_usbc.c — USB-Serial-JTAG (tethered) app data channel.
#include "pibud_usbc.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pibud_line.h"
#include "pibud_protocol.h"
#include "pibud_types.h"

static const char *TAG = "pibud_usbc";
#define PIBUD_USBC_DEV "/dev/usbserjtag"
#define PIBUD_USBC_RX_TASK_STACK 8192
#define PIBUD_USBC_RX_TASK_PRIO 4
#define PIBUD_USBC_READ_CHUNK 512

static int s_fd = -1;
static QueueHandle_t s_queue;

// Kept at file scope (NOT on the task stack): pibud_line_buffer_t alone is
// 4097 bytes, which would overflow a small task stack. The RX task is a
// singleton, so these are safe to share.
static pibud_line_buffer_t s_lb;
static uint8_t s_chunk[PIBUD_USBC_READ_CHUNK];

// pibud_line callback: consume each completed JSON line. Console log lines
// never start with '{', so they are skipped (return true = keep consuming).
static bool usbc_on_line(const char *line, size_t length, void *context)
{
    (void)context;
    if (length == 0 || line[0] != '{') {
        return true;
    }
    pibud_event_t ev;
    memset(&ev, 0, sizeof(ev));
    if (pibud_protocol_parse(line, &ev) != PIBUD_PROTO_OK) {
        return true; // unknown/malformed -> ignore, keep reading
    }
    ev.ble.connection_generation = 0; // USB transport: no BLE generation
    (void)xQueueSend(s_queue, &ev, 0);
    return true;
}

static void usbc_rx_task(void *arg)
{
    (void)arg;
    pibud_line_init(&s_lb);

    for (;;) {
        if (s_fd >= 0) {
            ssize_t n = read(s_fd, s_chunk, sizeof(s_chunk));
            if (n > 0) {
                (void)pibud_line_push(&s_lb, s_chunk, (size_t)n, usbc_on_line, NULL);
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGW(TAG, "usbserjtag read err %d; reopening", (int)errno);
                close(s_fd);
                s_fd = -1;
            }
        }
        if (s_fd < 0) {
            int fd = open(PIBUD_USBC_DEV, O_RDWR);
            if (fd >= 0) {
                s_fd = fd;
                pibud_line_init(&s_lb);
                ESP_LOGI(TAG, "usbserjtag channel open");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t pibud_usbc_init(QueueHandle_t event_queue)
{
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_queue = event_queue;
    s_fd = open(PIBUD_USBC_DEV, O_RDWR);
    if (s_fd < 0) {
        ESP_LOGW(TAG, "open %s failed; USB data channel unavailable", PIBUD_USBC_DEV);
        return ESP_FAIL;
    }
    if (xTaskCreate(usbc_rx_task, "pibud_usbc", PIBUD_USBC_RX_TASK_STACK, NULL,
                    PIBUD_USBC_RX_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "usbc rx task create failed");
        close(s_fd);
        s_fd = -1;
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "usb data channel ready");
    return ESP_OK;
}

esp_err_t pibud_usbc_send(const char *data, size_t length)
{
    if (s_fd < 0 || data == NULL) {
        return ESP_FAIL;
    }
    size_t off = 0;
    while (off < length) {
        ssize_t w = write(s_fd, data + off, length - off);
        if (w <= 0) {
            return ESP_FAIL;
        }
        off += (size_t)w;
    }
    return ESP_OK;
}

bool pibud_usbc_is_open(void)
{
    return s_fd >= 0;
}
