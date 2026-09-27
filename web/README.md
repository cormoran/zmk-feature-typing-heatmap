# Typing heatmap Web UI

Connect a keyboard running `zmk-feature-typing-heatmap` over USB (Web Serial)
or Bluetooth (Web Bluetooth) using Chrome or Edge on HTTPS or localhost.
The keyboard must enable ZMK Studio and the module's custom Studio RPC.
USB reconnects once automatically if a previously paired port is available.
Some keyboards need `&studio_unlock` before appearing in the Bluetooth picker.

The screen loads statistics once after connecting. **Refresh** reads a fresh
snapshot; the app does not poll. Each cell is a ZMK key position index, not a
physical layout or key label. **Export JSON** downloads the displayed snapshot,
including counters, save policy and total presses. **Reset statistics** requires
confirmation and clears both current counts and the stored snapshot.

**Save statistics across restarts** controls keyboard storage. Automatic saves
require both the displayed interval since the last save attempt (default 1800
seconds) and minimum new presses
(default 100). Unsaved presses are lost on restart or power loss. Memory mode
keeps counts for the current session only; switching to it removes the previous
saved statistics. Enabling persistence starts a new save interval. Reset and mode
changes write a small record immediately. Firmware without storage support shows
a disabled persistence checkbox and an explanation.

A storage error preserves RAM statistics and is shown explicitly. If counts change
between pages, the app retries up to three complete reads, then asks you to pause
typing and Refresh. Failed reset/mode operations must be retried explicitly.
Secured firmware variants show unlock guidance when an RPC is rejected; the
default module RPC works while Studio is locked.

## Development

```bash
NPM_CONFIG_ALLOW_GIT=all npm ci
npm run generate
npm run dev
npm run lint
npm test -- --runInBand
VITE_BASE=/ npm run build
VITE_BASE=/zmk-feature-typing-heatmap/ npm run build
```

Protocol types are generated from `../proto` via `buf.gen.yaml`; do not edit them
by hand. `src/heatmap.ts` owns the stable codec and bounded page reader.
`src/TypingHeatmapSection.tsx` uses scalar subsystem index and connection loader
identities, preventing render loops and ignoring old session responses. Unit tests
cover connection buttons, stable loading, pagination, mutations, unsupported
storage, overlapping actions and stale connections.

The integration test uses real firmware in Renode:

```bash
west zmk-build tests/zmk-config -af web_e2e
west zmk-web-e2e --elf build/web_e2e/zephyr/zmk.elf -- npm --prefix web run e2e
```

It checks connection, statistics, Refresh, confirmed Reset, persistence changes,
JSON download and Disconnect. Only the runner's serial transport shim is used;
statistics RPCs are handled by actual firmware.
