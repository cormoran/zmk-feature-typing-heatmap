/* SPDX-License-Identifier: MIT */
#include "typing_heatmap_state.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void write_u32(uint8_t *p, uint32_t value) {
    for (unsigned int i = 0; i < 4; i++) {
        p[i] = value >> (8 * i);
    }
}
void heatmap_init(struct heatmap_state *s, uint32_t *counts, uint32_t count, bool persistent) {
    *s =
        (struct heatmap_state){.counts = counts, .position_count = count, .persistent = persistent};
    memset(counts, 0, sizeof(*counts) * count);
}
void heatmap_press(struct heatmap_state *s, uint32_t position, bool pressed) {
    if (!pressed || position >= s->position_count || s->counts[position] == UINT32_MAX) {
        return;
    }
    s->counts[position]++;
    s->generation++;
    if (s->persistent && s->dirty != UINT64_MAX) {
        s->dirty++;
    }
}
bool heatmap_due(const struct heatmap_state *s, uint64_t now_ms, uint32_t interval_seconds,
                 uint32_t min_presses) {
    return s->persistent && s->dirty >= min_presses &&
           now_ms - s->last_attempt_ms >= (uint64_t)interval_seconds * 1000;
}
size_t heatmap_encode(const struct heatmap_state *s, uint8_t *blob, bool with_counts,
                      bool persistent) {
    write_u32(blob, 1);
    write_u32(blob + 4, s->position_count);
    write_u32(blob + 8, persistent);
    if (with_counts) {
        for (uint32_t i = 0; i < s->position_count; i++) {
            write_u32(blob + HEATMAP_HEADER_SIZE + 4 * i, s->counts[i]);
        }
    }
    return HEATMAP_HEADER_SIZE + (with_counts ? 4 * s->position_count : 0);
}
int heatmap_restore(struct heatmap_state *s, const uint8_t *blob, size_t length) {
    size_t full_size = HEATMAP_HEADER_SIZE + 4 * s->position_count;
    if ((length != HEATMAP_HEADER_SIZE && length != full_size) || read_u32(blob) != 1 ||
        read_u32(blob + 4) != s->position_count || read_u32(blob + 8) > 1 ||
        (!read_u32(blob + 8) && length != HEATMAP_HEADER_SIZE)) {
        return -EINVAL;
    }
    s->persistent = read_u32(blob + 8);
    /* Restore once at boot; input may already have arrived before settings_load(). */
    if (length == full_size) {
        for (uint32_t i = 0; i < s->position_count; i++) {
            uint32_t stored = read_u32(blob + HEATMAP_HEADER_SIZE + 4 * i);
            s->counts[i] = stored > UINT32_MAX - s->counts[i] ? UINT32_MAX : s->counts[i] + stored;
        }
    }
    if (!s->persistent) {
        s->dirty = 0;
    }
    s->generation++;
    return 0;
}
void heatmap_saved(struct heatmap_state *s, uint64_t dirty_snapshot, uint64_t now_ms, int error) {
    s->storage_error = error;
    s->last_attempt_ms = now_ms;
    if (!error) {
        s->dirty = s->dirty >= dirty_snapshot ? s->dirty - dirty_snapshot : 0;
    }
}
void heatmap_reset_done(struct heatmap_state *s, uint64_t now_ms, int error) {
    s->storage_error = error;
    if (!error) {
        memset(s->counts, 0, 4 * s->position_count);
        s->dirty = 0;
        s->last_attempt_ms = now_ms;
        s->generation++;
    }
}
void heatmap_mode_done(struct heatmap_state *s, bool enabled, uint64_t now_ms, int error) {
    s->storage_error = error;
    if (!error) {
        s->persistent = enabled;
        s->dirty = 0;
        s->last_attempt_ms = now_ms;
        s->generation++;
    }
}
int heatmap_page(const struct heatmap_state *s, uint32_t offset, uint32_t *counts, size_t *length) {
    if (offset >= s->position_count) {
        return -EINVAL;
    }
    *length = s->position_count - offset;
    if (*length > HEATMAP_PAGE_SIZE) {
        *length = HEATMAP_PAGE_SIZE;
    }
    memcpy(counts, s->counts + offset, *length * sizeof(*counts));
    return 0;
}
