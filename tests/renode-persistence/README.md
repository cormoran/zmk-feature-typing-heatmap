# Persistence integration test

This test runs real ARM firmware in Renode with automatic-save thresholds of
60 seconds and two presses. It checks actual key events and automatic NVS
checkpointing, copies only the NVS flash partition into a fresh emulator,
and verifies that boot restores the saved counts. It then enables RAM-only
mode, adds a press, reboots with the resulting flash, and verifies zero counts
and persisted RAM-only mode.

Use the `usb_wired_persistence` central and normal wired peripheral images:

```sh
west zmk-renode-test tests/renode-persistence --mode wired-split \
  --elf build/usb_wired_persistence/zephyr/zmk.elf \
  --peripheral-elf build/usb_wired_peripheral/zephyr/zmk.elf
```

Run one emulator test at a time on constrained hosts. The test fails when
Renode or the images are missing, when a checkpoint does not complete, or
when restored data differs. It does not substitute firmware RAM snapshots
for persistent storage.
