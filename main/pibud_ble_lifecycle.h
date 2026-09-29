#pragma once

#include <stdbool.h>

typedef struct {
    void *context;
    int (*host_init)(void *context);
    int (*events_init)(void *context);
    int (*bond_callout_init)(void *context);
    int (*adv_callout_init)(void *context);
    int (*gatt_init)(void *context);
    void (*adv_callout_deinit)(void *context);
    void (*bond_callout_deinit)(void *context);
    void (*events_deinit)(void *context);
    int (*host_deinit)(void *context);
    void (*clear)(void *context);
} pibud_ble_lifecycle_ops_t;

int pibud_ble_lifecycle_init(const pibud_ble_lifecycle_ops_t *ops, bool *retry_safe);
bool pibud_ble_lifecycle_retry_allowed(bool initialized, bool rollback_blocked);
