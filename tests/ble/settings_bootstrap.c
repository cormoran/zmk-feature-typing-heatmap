/* SPDX-License-Identifier: MIT */
#include <zephyr/init.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

/* This test baseline registers BLE profile handlers before main initializes
 * settings, which clears those handlers and prevents central advertising.
 * Initialize the idempotent settings subsystem first for this fixture only.
 */
BUILD_ASSERT(CONFIG_ZMK_BLE_INIT_PRIORITY > 40,
             "BLE fixture settings bootstrap must precede BLE initialization");
static int bootstrap_settings(void) { return settings_subsys_init(); }
SYS_INIT(bootstrap_settings, APPLICATION, 40);
