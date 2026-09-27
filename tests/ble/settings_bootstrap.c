/* SPDX-License-Identifier: MIT */
#include <zephyr/init.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(typing_heatmap_ble_fixture, LOG_LEVEL_INF);

/* Native linking preserves the readonly flags of ZMK's const map entries.
 * A kept, empty writable input section makes the output registry writable
 * without adding any entries or changing the dependency checkout.
 */
__asm__(".pushsection ._zmk_behavior_local_id_map.static.fixture,\"aw\",@progbits\n"
        ".popsection\n");

/* This test baseline registers BLE profile handlers before main initializes
 * settings, which clears those handlers and prevents central advertising.
 * Initialize the idempotent settings subsystem first for this fixture only.
 */
BUILD_ASSERT(CONFIG_ZMK_BLE_INIT_PRIORITY > 40,
             "BLE fixture settings bootstrap must precede BLE initialization");
static int bootstrap_settings(void) {
    int rc = settings_subsys_init();
    LOG_INF("settings bootstrap rc=%d", rc);
    return rc;
}
SYS_INIT(bootstrap_settings, APPLICATION, 40);
