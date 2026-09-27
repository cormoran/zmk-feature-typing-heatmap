/* SPDX-License-Identifier: MIT */
#include "typing_heatmap_state.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    uint32_t counts[20], page[16];
    uint8_t blob[HEATMAP_HEADER_SIZE + sizeof(counts)];
    struct heatmap_state state;
    heatmap_init(&state, counts, 20, true);
    heatmap_press(&state, 0, false);
    heatmap_press(&state, 20, true);
    assert(state.dirty == 0 && state.generation == 0);
    for (int i = 0; i < 100; i++) {
        heatmap_press(&state, 0, true);
    }
    assert(counts[0] == 100 && state.dirty == 100);
    assert(!heatmap_due(&state, 1799999, 1800, 100));
    assert(heatmap_due(&state, 1800000, 1800, 100));
    assert(!heatmap_due(&state, 1800000, 1800, 101));
    uint64_t snapshot_dirty = state.dirty;
    size_t length = heatmap_encode(&state, blob, true, true);
    assert(length == sizeof(blob));
    assert(blob[0] == 1 && blob[4] == 20 && blob[8] == 1 && blob[12] == 100);
    heatmap_press(&state, 1, true);
    heatmap_saved(&state, snapshot_dirty, 1800000, -EIO);
    assert(state.dirty == 101 && state.last_attempt_ms == 1800000 && state.storage_error == -EIO);
    assert(!heatmap_due(&state, 1860000, 1800, 100));
    assert(heatmap_due(&state, 3600000, 1800, 100));
    heatmap_saved(&state, snapshot_dirty, 1800000, 0);
    assert(state.dirty == 1 && counts[1] == 1 && state.last_attempt_ms == 1800000);
    size_t page_length = 0;
    assert(heatmap_page(&state, 0, page, &page_length) == 0 && page_length == 16);
    assert(page[0] == 100 && page[1] == 1);
    assert(heatmap_page(&state, 16, page, &page_length) == 0 && page_length == 4);
    assert(heatmap_page(&state, 20, page, &page_length) == -EINVAL);
    assert(heatmap_page(&state, UINT32_MAX, page, &page_length) == -EINVAL);
    uint32_t generation = state.generation;
    heatmap_reset_done(&state, 2000000, -EIO);
    assert(counts[0] == 100 && state.dirty == 1 && state.generation == generation);
    heatmap_mode_done(&state, false, 2000000, -EIO);
    assert(state.persistent && counts[0] == 100 && state.generation == generation);
    heatmap_mode_done(&state, false, 2000000, 0);
    assert(!state.persistent && counts[0] == 100 && state.dirty == 0);
    heatmap_press(&state, 1, true);
    assert(counts[1] == 2 && state.dirty == 0);
    assert(!heatmap_due(&state, 9999999, 1800, 100));
    length = heatmap_encode(&state, blob, false, false);
    assert(length == HEATMAP_HEADER_SIZE);
    heatmap_init(&state, counts, 20, true);
    assert(heatmap_restore(&state, blob, length) == 0);
    assert(!state.persistent && counts[0] == 0 && counts[1] == 0);
    heatmap_press(&state, 0, true);
    heatmap_mode_done(&state, true, 3000000, 0);
    assert(state.persistent && counts[0] == 1 && state.dirty == 0);
    assert(!heatmap_due(&state, 9999999, 1800, 100));
    length = heatmap_encode(&state, blob, false, true);
    heatmap_init(&state, counts, 20, true);
    assert(heatmap_restore(&state, blob, length) == 0 && state.persistent && counts[0] == 0);
    counts[0] = UINT32_MAX - 1;
    heatmap_press(&state, 0, true);
    assert(counts[0] == UINT32_MAX);
    generation = state.generation;
    heatmap_press(&state, 0, true);
    assert(counts[0] == UINT32_MAX && state.generation == generation);
    length = heatmap_encode(&state, blob, true, true);
    heatmap_reset_done(&state, 4000000, 0);
    assert(counts[0] == 0 && state.dirty == 0);
    heatmap_init(&state, counts, 20, true);
    assert(heatmap_restore(&state, blob, length) == 0 && counts[0] == UINT32_MAX);
    blob[0] = 2;
    assert(heatmap_restore(&state, blob, length) == -EINVAL && counts[0] == UINT32_MAX);
    blob[0] = 1;
    blob[4] = 21;
    assert(heatmap_restore(&state, blob, length) == -EINVAL);
    blob[4] = 20;
    blob[8] = 2;
    assert(heatmap_restore(&state, blob, length) == -EINVAL);
    blob[8] = 0;
    assert(heatmap_restore(&state, blob, length) == -EINVAL);
    assert(heatmap_restore(&state, blob, 0) == -EINVAL);
    assert(heatmap_restore(&state, blob, 11) == -EINVAL);
    assert(heatmap_restore(&state, blob, 13) == -EINVAL);
    /* Events observed before main/settings_load must survive the boot restore. */
    heatmap_init(&state, counts, 20, true);
    for (int i = 0; i < 7; i++) {
        heatmap_press(&state, 0, true);
    }
    length = heatmap_encode(&state, blob, true, true);
    heatmap_init(&state, counts, 20, true);
    heatmap_press(&state, 0, true);
    heatmap_press(&state, 0, true);
    assert(heatmap_restore(&state, blob, length) == 0);
    assert(counts[0] == 9 && state.dirty == 2 && state.persistent);
    length = heatmap_encode(&state, blob, false, false);
    heatmap_init(&state, counts, 20, true);
    heatmap_press(&state, 0, true);
    assert(heatmap_restore(&state, blob, length) == 0);
    assert(counts[0] == 1 && state.dirty == 0 && !state.persistent);
    length = heatmap_encode(&state, blob, false, true);
    heatmap_init(&state, counts, 20, true);
    heatmap_press(&state, 0, true);
    assert(heatmap_restore(&state, blob, length) == 0);
    assert(counts[0] == 1 && state.dirty == 1 && state.persistent);
    counts[0] = UINT32_MAX;
    length = heatmap_encode(&state, blob, true, true);
    heatmap_init(&state, counts, 20, true);
    heatmap_press(&state, 0, true);
    assert(heatmap_restore(&state, blob, length) == 0);
    assert(counts[0] == UINT32_MAX && state.dirty == 1);
    puts("heatmap state: PASS");
}
