/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <limits.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/matrix.h>
#include <zmk/workqueue.h>
#include <cormoran/feature-typing-heatmap/typing_heatmap.h>
#include "typing_heatmap_state.h"

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#if IS_ENABLED(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static uint32_t counters[ZMK_KEYMAP_LEN];
static struct heatmap_state state;
static struct k_spinlock state_lock;
K_MUTEX_DEFINE(storage_lock);
static bool ready;
/* The flash write buffer is not on the workqueue's small stack. */
static uint8_t storage_blob[HEATMAP_HEADER_SIZE + sizeof(counters)];
#if IS_ENABLED(CONFIG_SETTINGS)
static size_t loaded_size;
static bool loaded;
#endif
static struct k_work_delayable checkpoint_work;
#if IS_ENABLED(CONFIG_ZMK_FEATURE_TYPING_HEATMAP_TEST)
static struct k_work_delayable test_work;
#endif

static uint64_t now_ms(void) { return (uint64_t)k_uptime_get(); }
static int store_record(size_t length) {
#if IS_ENABLED(CONFIG_SETTINGS)
    return settings_save_one("typing_heatmap/state", storage_blob, length);
#else
    ARG_UNUSED(length);
    return -ENOTSUP;
#endif
}

int typing_heatmap_get_stats(uint32_t offset, struct typing_heatmap_stats *stats) {
    if (!stats) {
        return -EINVAL;
    }
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    int rc = heatmap_page(&state, offset, stats->counts, &stats->count);
    if (!rc) {
        stats->offset = offset;
        stats->position_count = state.position_count;
        stats->persistence_enabled = state.persistent;
        stats->persistence_supported = IS_ENABLED(CONFIG_SETTINGS);
        stats->unsaved_presses = MIN(state.dirty, UINT32_MAX);
        stats->save_interval_seconds = CONFIG_ZMK_FEATURE_TYPING_HEATMAP_SAVE_INTERVAL_SECONDS;
        stats->min_presses = CONFIG_ZMK_FEATURE_TYPING_HEATMAP_MIN_PRESSES;
        stats->storage_error = state.storage_error;
        stats->generation = state.generation;
    }
    k_spin_unlock(&state_lock, key);
    return rc;
}

int typing_heatmap_reset(void) {
    k_mutex_lock(&storage_lock, K_FOREVER);
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    if (!ready) {
        k_spin_unlock(&state_lock, key);
        k_mutex_unlock(&storage_lock);
        return -EAGAIN;
    }
    size_t length = heatmap_encode(&state, storage_blob, false, state.persistent);
    k_spin_unlock(&state_lock, key);
    /* Reset the persisted data even when it is below automatic thresholds. */
    int rc = IS_ENABLED(CONFIG_SETTINGS) ? store_record(length) : 0;
    key = k_spin_lock(&state_lock);
    heatmap_reset_done(&state, now_ms(), rc);
    k_spin_unlock(&state_lock, key);
    k_mutex_unlock(&storage_lock);
    return rc;
}

int typing_heatmap_set_persistence(bool enabled) {
    if (enabled && !IS_ENABLED(CONFIG_SETTINGS)) {
        return -ENOTSUP;
    }
    k_mutex_lock(&storage_lock, K_FOREVER);
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    if (!ready) {
        k_spin_unlock(&state_lock, key);
        k_mutex_unlock(&storage_lock);
        return -EAGAIN;
    }
    if (state.persistent == enabled) {
        k_spin_unlock(&state_lock, key);
        k_mutex_unlock(&storage_lock);
        return 0;
    }
    size_t length = heatmap_encode(&state, storage_blob, false, enabled);
    k_spin_unlock(&state_lock, key);
    /* One record changes the mode and invalidates old counts atomically. */
    int rc = store_record(length);
    key = k_spin_lock(&state_lock);
    heatmap_mode_done(&state, enabled, now_ms(), rc);
    k_spin_unlock(&state_lock, key);
    k_mutex_unlock(&storage_lock);
    return rc;
}

static void checkpoint(struct k_work *work) {
    ARG_UNUSED(work);
    k_mutex_lock(&storage_lock, K_FOREVER);
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool due = ready && heatmap_due(&state, now_ms(),
                                    CONFIG_ZMK_FEATURE_TYPING_HEATMAP_SAVE_INTERVAL_SECONDS,
                                    CONFIG_ZMK_FEATURE_TYPING_HEATMAP_MIN_PRESSES);
    uint64_t dirty_snapshot = state.dirty;
    size_t length = due ? heatmap_encode(&state, storage_blob, true, true) : 0;
    k_spin_unlock(&state_lock, key);
    if (due) {
        int rc = store_record(length);
        key = k_spin_lock(&state_lock);
        heatmap_saved(&state, dirty_snapshot, now_ms(), rc);
        k_spin_unlock(&state_lock, key);
        if (rc) {
            LOG_WRN("Typing statistics save failed: %d", rc);
        }
    }
    k_mutex_unlock(&storage_lock);
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &checkpoint_work, K_SECONDS(60));
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int settings_set(const char *name, size_t length, settings_read_cb read_cb, void *cb_arg) {
    if (strcmp(name, "state")) {
        return -ENOENT;
    }
    /* This owner applies only its initial boot load. */
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    bool already_ready = ready;
    k_spin_unlock(&state_lock, key);
    if (already_ready) {
        return 0;
    }
    if (length != HEATMAP_HEADER_SIZE && length != sizeof(storage_blob)) {
        key = k_spin_lock(&state_lock);
        state.storage_error = -EINVAL;
        k_spin_unlock(&state_lock, key);
        return -EINVAL;
    }
    int bytes = read_cb(cb_arg, storage_blob, length);
    if (bytes < 0 || (size_t)bytes != length) {
        int error = bytes < 0 ? bytes : -EIO;
        key = k_spin_lock(&state_lock);
        state.storage_error = error;
        k_spin_unlock(&state_lock, key);
        return error;
    }
    loaded_size = length;
    loaded = true;
    return 0;
}
static int heatmap_settings_commit(void) {
    k_spinlock_key_t key = k_spin_lock(&state_lock);
    if (!ready) {
        if (loaded) {
            state.storage_error = heatmap_restore(&state, storage_blob, loaded_size);
        }
        state.last_attempt_ms = now_ms();
        ready = true;
    }
    k_spin_unlock(&state_lock, key);
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(typing_heatmap, "typing_heatmap", NULL, settings_set,
                               heatmap_settings_commit, NULL);
#endif

#if IS_ENABLED(CONFIG_ZMK_FEATURE_TYPING_HEATMAP_TEST)
static void test_report(struct k_work *work) {
    ARG_UNUSED(work);
    struct typing_heatmap_stats stats;
    if (!typing_heatmap_get_stats(0, &stats)) {
        for (size_t i = 0; i < stats.count; i++) {
            LOG_INF("HEATMAP position=%u count=%u", (unsigned int)i, stats.counts[i]);
        }
    }
}
#endif

static int position_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *event = as_zmk_position_state_changed(eh);
    if (event) {
        k_spinlock_key_t key = k_spin_lock(&state_lock);
        heatmap_press(&state, event->position, event->state);
        k_spin_unlock(&state_lock, key);
#if IS_ENABLED(CONFIG_ZMK_FEATURE_TYPING_HEATMAP_TEST)
        k_work_reschedule(&test_work, K_MSEC(100));
#endif
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(typing_heatmap, position_listener);
ZMK_SUBSCRIPTION(typing_heatmap, zmk_position_state_changed);

static int init(void) {
    heatmap_init(&state, counters, ARRAY_SIZE(counters), IS_ENABLED(CONFIG_SETTINGS));
    ready = !IS_ENABLED(CONFIG_SETTINGS);
    k_work_init_delayable(&checkpoint_work, checkpoint);
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &checkpoint_work, K_SECONDS(60));
#if IS_ENABLED(CONFIG_ZMK_FEATURE_TYPING_HEATMAP_TEST)
    k_work_init_delayable(&test_work, test_report);
#endif
    return 0;
}
SYS_INIT(init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#else
int typing_heatmap_get_stats(uint32_t offset, struct typing_heatmap_stats *stats) {
    ARG_UNUSED(offset);
    ARG_UNUSED(stats);
    return -ENOTSUP;
}
int typing_heatmap_reset(void) { return -ENOTSUP; }
int typing_heatmap_set_persistence(bool enabled) {
    ARG_UNUSED(enabled);
    return -ENOTSUP;
}
#endif
