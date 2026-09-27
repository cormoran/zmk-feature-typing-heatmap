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

These features are under development on the implementation branch.

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
CONFIG_ZMK_CUSTOM_SETTINGS=y
CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y
CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=128
CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=2048
```

Build with a Studio USB transport, connect the keyboard in a Web Serial
compatible browser, and unlock Studio using your keyboard's `&studio_unlock`
binding. The Web UI retrieves position counts, resets them, and changes the
persistence mode. See [web/README.md](web/README.md) for frontend development.

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
