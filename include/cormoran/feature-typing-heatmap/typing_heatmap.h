/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TYPING_HEATMAP_PAGE_SIZE 16U
struct typing_heatmap_stats {
    uint32_t offset;
    uint32_t position_count;
    uint32_t counts[TYPING_HEATMAP_PAGE_SIZE];
    size_t count;
    bool persistence_enabled;
    bool persistence_supported;
    uint32_t unsaved_presses;
    uint32_t save_interval_seconds;
    uint32_t min_presses;
    int storage_error;
    uint32_t generation;
};
int typing_heatmap_get_stats(uint32_t offset, struct typing_heatmap_stats *stats);
int typing_heatmap_reset(void);
int typing_heatmap_set_persistence(bool enabled);
