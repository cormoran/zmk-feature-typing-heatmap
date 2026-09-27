# BLE fixture

Run `west zmk-ble-test tests/ble -m .`, then `python3 tests/ble/validate.py`.
The normal unittest BLE gate and GitHub BLE job run both commands.

The snapshot requires the host to finish every request and the peripheral to
receive the inherited sample relay. The validator decodes actual protobuf
response bytes and compares JSON expectations for discovery, sample RPC,
statistics, reset, persistence changes, and an invalid page. It ignores changing
packet framing, generation numbers, advertiser addresses, and GATT handles.

The selected `main+custom-studio-protocol` ZMK baseline registers its dynamic
BLE profile settings handler during SYS_INIT, before `main()` initializes the
settings subsystem. That later initialization clears the handler and prevents
the central from completing BLE startup. `settings_bootstrap.c`, compiled only
with the central fixture's `CONFIG_ZMK_FEATURE_TYPING_HEATMAP_BLE_TEST`, calls
the idempotent settings initialization before BLE initialization. The flag
requires ARCH_POSIX and is not enabled in production firmware. Remove this shim
when the dependency baseline initializes settings before BLE registration.
