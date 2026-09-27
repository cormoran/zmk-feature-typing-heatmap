# zmk-feature-typing-heatmap

A ZMK typing statistics module with a Web UI using the unofficial custom
ZMK Studio RPC protocol. It counts physical key presses by key position,
including positions forwarded from a split peripheral to the central.
Counts are independent of the active layer or the keycode assigned to a key.

## Features

- One saturating 32-bit counter per key position.
- Read statistics in pages through Studio RPC, or view them in the Web UI.
- Reset statistics and switch persistence mode from the Web UI.
- Persistent mode saves at most once every 30 minutes, and only after at
  least 100 new presses. Recent unsaved presses can be lost on reboot.
- RAM-only mode keeps statistics until reboot without writing press counts
  to flash. Switching to RAM-only clears the previous saved statistics.
- Storage uses one compact versioned record containing the counter array;
  it does not store timestamps or individual key events.

## Module User Guide

This module requires cormoran's patched ZMK branch with custom Studio RPC
support. Add these projects and the remote to your configuration's west
manifest, merging them into existing entries rather than duplicating ZMK:

```yaml
manifest:
  remotes:
    - name: cormoran
      url-base: https://github.com/cormoran
  projects:
    - name: zmk-feature-typing-heatmap
      remote: cormoran
      revision: main
      import: true
    - name: zmk
      remote: cormoran
      revision: main+custom-studio-protocol
      import:
        file: app/west.yml
```

Enable the module in your keyboard configuration (on the central for a split):

```conf
CONFIG_ZMK_FEATURE_TYPING_HEATMAP=y
CONFIG_ZMK_STUDIO=y
CONFIG_ZMK_FEATURE_TYPING_HEATMAP_STUDIO_RPC=y
CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=128
CONFIG_ZMK_STUDIO_RPC_TX_BUF_SIZE=256
CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=2048
```

Build with the `studio-rpc-usb-uart` snippet (or your keyboard's Studio USB
transport). Open the [Web UI](https://cormoran.github.io/zmk-feature-typing-heatmap/)
in Chrome or Edge, connect via USB or supported Studio Bluetooth, and press
**Refresh** to retrieve statistics. Counters are indexed by ZMK key position,
starting at zero; the grid is an index view, not your keyboard's physical layout.
**Export JSON** downloads the current snapshot. **Reset statistics** asks for
confirmation and clears both live and stored statistics. The module's RPC is
unsecured by default; a firmware variant using secured handlers requires your
keyboard's `&studio_unlock` binding. See [web/README.md](web/README.md).

### Configure automatic saves

Optional compile-time overrides (defaults shown):

```conf
CONFIG_ZMK_FEATURE_TYPING_HEATMAP_SAVE_INTERVAL_SECONDS=1800
CONFIG_ZMK_FEATURE_TYPING_HEATMAP_MIN_PRESSES=100
```

The interval accepts 60–86,400 seconds and the threshold 1–1,000,000 presses.
Choose a long interval to reduce flash wear. The Web UI reports both values.

### Persistence and data loss

The module counts presses on the central or standalone keyboard; split
peripherals require no module configuration. Releases, held-key repeats from
the host, and synthetic macro keycodes do not add counts. Each position's
counter saturates at 4,294,967,295 instead of wrapping.

Both the interval and minimum new presses must be met for an automatic save.
The interval also limits retries after a failed automatic save, preserving
low write frequency. The defaults limit automatic save attempts to at most
48 per day of continuous use. Low activity below the minimum is intentionally left unsaved. Rebooting
can lose all presses since the last successful checkpoint; there is no
shutdown or disconnect save.

Turning **Persistence** off keeps the current live counts but replaces saved
statistics with a header-only RAM-mode record. Further presses stay in RAM,
and every boot in that mode starts at zero. Turning it back on preserves live
counts, starts a fresh interval, and requires the minimum number of subsequent
presses before the next checkpoint. Mode switches and explicit resets perform
one intentional record write regardless of the automatic save thresholds.
Failures are reported without claiming that a reset or mode change succeeded.
Firmware with `CONFIG_SETTINGS=n` supports RAM-only statistics without Studio
RPC. The current Studio implementation selects settings support; its Web UI
can still switch the statistics to RAM mode.

The stored record is 12 bytes plus 4 bytes per position (412 bytes for 100
positions), excluding Zephyr's storage bookkeeping. No per-press history or
timestamps are stored. A record for a different position count or format
version is rejected. If you change a layout while keeping the same position
count, reset the statistics yourself so old counts are not attributed to new
positions. Counter arrays track the largest compiled physical layout.

Only counts, mode and format metadata belong to this compact record. The
module does not repeatedly persist runtime preferences through the generic
custom-settings registry; mode and counts share one atomic settings record.

## Development

Follow [AGENTS.md](AGENTS.md) and the repository skills. In this
zmk-workspace, select a compatible shared West profile and use a worktree
under it. Run firmware commands in the workspace Nix devshell, and keep all
build output in the worktree's own `build/` directory.

```sh
python3 scripts/init_module.py --verify-only
python3 -m unittest -v
west zmk-build tests/zmk-config -m . -d ./build -q
west zmk-test tests -m . -d ./build
```

Run frontend checks outside the Nix devshell:

```sh
cd web
NPM_CONFIG_ALLOW_GIT=all npm ci
npm run generate
npm run lint
npm test -- --runInBand
VITE_BASE=/ npm run build
VITE_BASE=/zmk-feature-typing-heatmap/ npm run build
```

### Hardware-free Renode testing

The test configuration includes USB wired central/peripheral images. Run:

```sh
west zmk-renode-test tests/renode --mode wired-split \
  --elf build/usb_wired_central/zephyr/zmk.elf \
  --peripheral-elf build/usb_wired_peripheral/zephyr/zmk.elf
```

### Web UI end-to-end testing

The browser end-to-end test connects to real firmware running in Renode:

```sh
west zmk-web-e2e --elf build/web_e2e/zephyr/zmk.elf -- npm --prefix web run e2e
```

### Running BLE (BabbleSim) tests

When a BabbleSim environment is available:

```sh
west zmk-ble-test tests/ble -m .
```
