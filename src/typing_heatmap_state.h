/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEATMAP_HEADER_SIZE 12U
#define HEATMAP_PAGE_SIZE 16U
struct heatmap_state {
    uint32_t *counts;
    uint32_t position_count;
    uint32_t generation;
    uint64_t dirty;
    uint64_t last_attempt_ms;
    bool persistent;
    int storage_error;
};
void heatmap_init(struct heatmap_state *s, uint32_t *counts, uint32_t count, bool persistent);
void heatmap_press(struct heatmap_state *s, uint32_t position, bool pressed);
bool heatmap_due(const struct heatmap_state *s, uint64_t now_ms, uint32_t interval_seconds,
                 uint32_t min_presses);
size_t heatmap_encode(const struct heatmap_state *s, uint8_t *blob, bool with_counts,
                      bool persistent);
int heatmap_restore(struct heatmap_state *s, const uint8_t *blob, size_t length);
void heatmap_saved(struct heatmap_state *s, uint64_t dirty_snapshot, uint64_t now_ms, int error);
void heatmap_reset_done(struct heatmap_state *s, uint64_t now_ms, int error);
void heatmap_mode_done(struct heatmap_state *s, bool enabled, uint64_t now_ms, int error);
int heatmap_page(const struct heatmap_state *s, uint32_t offset, uint32_t *counts, size_t *length);
