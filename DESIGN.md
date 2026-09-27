# Typing heatmap

## Scope
Count physical ZMK position press events once on the central or standalone keyboard. Ignore releases and synthetic keycodes; split peripherals do not maintain a second copy. Counts saturate at UINT32_MAX. The current keymap position count determines the array length.

## Storage
One versioned little-endian settings blob: 12-byte header (version, position count, persistence mode), followed by one uint32 per position. No history or timestamps. RAM uses the live counters plus a save snapshot. Reject blobs with a different position count/version/length rather than silently attaching counts to other keys. Layout changes with the same position count require a user reset.

Default automatic checkpoint interval: 1800 seconds; minimum unsaved presses: 100. Both conditions must be true. A periodic work item checks eligibility; no write on keypress, disconnect, or shutdown. Serialize storage operations and resets/mode changes; snapshot before writing so concurrent presses remain dirty. Failed writes preserve dirty data and expose an error. Save after settings boot load; do not use SYS_INIT to read settings before settings_load.

Persistence mode defaults on. Turning it off atomically replaces the stored snapshot with a header-only off-mode record; live session counts remain, and subsequent boots in RAM mode start at zero. Turning it on atomically writes a header-only on-mode record and starts the checkpoint interval without forcing a statistics save. Explicit reset zeroes counters and atomically replaces the snapshot with a header-only record immediately, even below thresholds; fail without changing counters if writing fails. Mode transitions and reset are intentional user writes exempt from automatic thresholds. Prefer custom-settings for future independently configurable preferences; here mode and snapshot must be serialized, so a single native settings record atomically handles mode and counts.

## Protocol
Subsystem identifier: `cormoran_typing_heatmap` (fits the ZMK identifier length limit).
Keep template Sample RPC temporarily for its template validation harness. Add GetStats(offset) -> Stats containing up to 16 counters, offset, position_count, persistence mode, unsaved presses, interval, threshold, storage error and generation. Reset and SetPersistence return status. A generation increments on each press/reset/mode change: web retries a complete read when pages disagree, bounded retries with a useful error during continuous typing. No 64-bit proto values. Bounded repeated counts in nanopb options; static RPC response storage; TX build assertion and at least 256-byte buffers.

## Web
Stable module-scope codec, stable scalar subsystem identity. Fetch on explicit Refresh and once after connection. Display position-indexed cells shaded by relative counts, total as JavaScript number, export JSON, reset confirmation, mode toggle and clear loss/save policy. Preserve USB/BLE connection flow. Never poll continuously. Fetch again after reset/mode change; disable overlapping actions. Unit tests cover pagination, mutation and loader stability; e2e exercises real RPC.

## Phases
A: initialize template and verify inherited baseline.
B: core counters, settings lifecycle, paged RPC, native tests and build matrix.
C: Web UI and tests, protocol generation, Renode RPC and browser verification.
D: full gates, commit, push, PR, monitor CI. Hardware validation only if needed after emulator coverage.
