# BLE fixture

Run `west zmk-ble-test tests/ble -m .`, then `python3 tests/ble/validate.py`.
The normal unittest BLE gate and GitHub BLE job run both commands.

The snapshot requires the host to finish every request and the peripheral to
receive the inherited sample relay. The validator decodes actual protobuf
response bytes and compares JSON expectations for discovery, sample RPC,
statistics, failed reset/persistence writes, unchanged counters/mode after those
failures, and an invalid page. BabbleSim uses SETTINGS_NONE, so mutation calls
that require storage must return ENOENT; the Renode suite separately verifies
successful NVS persistence, reset, and reboot behavior. It ignores changing
packet framing, generation numbers, advertiser addresses, and GATT handles.

The selected `main+custom-studio-protocol` ZMK baseline registers its dynamic
BLE profile settings handler during SYS_INIT, before `main()` initializes the
settings subsystem. That later initialization clears the handler and prevents
the central from completing BLE startup. `settings_bootstrap.c`, compiled only
with the central fixture's `CONFIG_ZMK_FEATURE_TYPING_HEATMAP_BLE_TEST`, calls
the idempotent settings initialization before BLE initialization. The flag
requires ARCH_POSIX and is not enabled in production firmware. Remove this shim
when the dependency baseline initializes settings before BLE registration.

The baseline also declares behavior local-ID map entries const. The native
simulator's final ELF therefore makes that registry readonly even though ZMK's
linker snippet requests RAM, and Studio startup faults while assigning IDs.
The fixture adds an empty writable input section matching the iterable registry
KEEP glob. It contributes no entries or bytes and makes the native output
section writable. Remove it when the dependency declares that map writable.
