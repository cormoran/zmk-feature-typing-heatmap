#!/usr/bin/env python3
"""Actual NVS checkpoint/boot restore and RAM-mode reboot integration test.

Run with the 60-second / two-press usb_wired_persistence ARM image:
west zmk-renode-test tests/renode-persistence --mode wired-split \
  --elf build/usb_wired_persistence/zephyr/zmk.elf \
  --peripheral-elf build/usb_wired_peripheral/zephyr/zmk.elf

Each reboot starts a fresh emulator with only the previous machine's NVS
flash bytes restored, so no firmware RAM or RPC client state survives.
"""

from __future__ import annotations

import importlib.util
import os
import random
import sys
import tempfile
import time
import unittest
from contextlib import ExitStack
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "heatmap_rpc_helpers", ROOT / "tests/renode/renode_test.py"
)
helpers = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helpers)
h = helpers.renode_harness


class PersistenceTests(unittest.TestCase):
    # Reuse only transport methods, never inherit the unrelated template tests.
    _send_call = helpers.RenodeWiredSplitModuleTests._send_call
    _read_response = helpers.RenodeWiredSplitModuleTests._read_response
    _heatmap_call = helpers.RenodeWiredSplitModuleTests._heatmap_call

    def setUp(self):
        self.renode_path = h.find_or_install_renode()
        self.assertIsNotNone(
            self.renode_path, "Renode is required for this integration test"
        )
        self.central_elf = Path(os.environ["ZMK_RENODE_ELF"])
        self.peripheral_elf = Path(os.environ["ZMK_RENODE_PERIPHERAL_ELF"])
        self.assertTrue(self.central_elf.is_file())
        self.assertTrue(self.peripheral_elf.is_file())
        self.storage_addr = int(
            os.environ.get("ZMK_RENODE_STORAGE_ADDR") or hex(h.STORAGE_ADDR_DEFAULT), 0
        )
        self.storage_size = int(
            os.environ.get("ZMK_RENODE_STORAGE_SIZE") or hex(h.STORAGE_SIZE_DEFAULT), 0
        )
        self.studio_pb2 = h.load_studio_pb2(helpers.find_module_studio_protos())
        directory = h.compile_protos(
            [
                ROOT
                / "proto/cormoran/feature-typing-heatmap/feature_typing_heatmap.proto"
            ],
            [ROOT / "proto"],
        )
        sys.path.insert(0, str(directory))
        from cormoran.feature_typing_heatmap import feature_typing_heatmap_pb2

        self.template_pb2 = feature_typing_heatmap_pb2
        self.temporary = tempfile.TemporaryDirectory(prefix="heatmap-nvs-")
        self.addCleanup(self.temporary.cleanup)
        self.live = None
        self.addCleanup(self.close_machine)

    def close_machine(self):
        if self.live:
            self.live.close()
            self.live = None

    def boot(self, flash: Path | None = None):
        self.close_machine()
        resources = ExitStack()
        self.live = resources
        port = random.randint(26000, 40000)
        # The standard harness erases NVS on every boot. Build the same session
        # here, replacing only that erased preload with the previous flash file.
        platform = Path(
            h._materialize_real_repl(template_name="xiao_nrf52840_usb.repl")
        )
        resources.callback(platform.unlink, missing_ok=True)
        self.session = h.RenodeSession(
            self.renode_path,
            h.PLATFORMS_DIR / "usb_wired_split.resc",
            port,
            {
                "central_bin": f"@{self.central_elf}",
                "peripheral_bin": f"@{self.peripheral_elf}",
                "central_platform": f"@{platform}",
                "central_console_port": port + 1,
                "peripheral_console_port": port + 2,
            },
            cwd=h.SKILL_DIR,
        )
        resources.callback(self.session.stop)
        self.session.start()
        console = self.session.connect_uart(port + 1)
        resources.callback(console.close)
        peripheral = self.session.connect_uart(port + 2)
        resources.callback(peripheral.close)
        self.session.mon.execute('mach set "central"')
        if flash is None:
            flash = Path(self.temporary.name) / "erased.bin"
            flash.write_bytes(b"\xff" * self.storage_size)
        self.session.mon.execute(f"sysbus LoadBinary @{flash} {hex(self.storage_addr)}")
        # Establish a released active-low switch before either CPU starts.
        for machine in ("central", "peripheral"):
            self.session.mon.execute(f'mach set "{machine}"')
            self.session.mon.execute("sysbus.gpio0 OnGPIO 2 true")
        self.session.mon.execute('mach set "central"')
        self.session.go()
        banner = h.wait_for_text(console._sock, "Welcome to ZMK", timeout=20)
        self.assertIn("Welcome to ZMK", banner)
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            h.drain_text(console._sock, timeout=0.5)
        cdc0, cdc1 = h.attach_dual_cdc_bridge(self.session, port + 4, port + 5)
        resources.callback(cdc0.close)
        resources.callback(cdc1.close)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if helpers._mon_is_true(self.session.mon, "sysbus.bridge_cdc0 IsWired"):
                break
        else:
            self.fail("USB CDC enumeration did not finish")
        self.studio = (
            cdc1
            if helpers._mon_is_true(self.session.mon, "sysbus.bridge_cdc1 IsWired")
            else cdc0
        )
        time.sleep(2)

    def stats(self):
        request = self.template_pb2.Request()
        request.get_stats.SetInParent()
        response = self._heatmap_call(request)
        self.assertEqual(response.WhichOneof("response_type"), "stats")
        return response.stats

    def flash_snapshot(self, name):
        path = Path(self.temporary.name) / name
        self.session.mon.execute('mach set "central"')
        self.session.mon.execute("pause")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if helpers._mon_is_true(self.session.mon, "machine IsPaused"):
                break
        else:
            self.fail("Central did not pause before NVS flash capture")
        # Read only flash, never RAM or an emulator snapshot. The monitor's
        # embedded Python calls the same SystemBus read exposed by ReadBytes.
        command = f"python \"import System; System.IO.File.WriteAllBytes('{path}', monitor.Machine.SystemBus.ReadBytes({self.storage_addr}, {self.storage_size}))\""
        result = self.session.mon.execute(command)
        self.assertTrue(path.is_file(), f"NVS dump failed: {result}")
        self.assertEqual(path.stat().st_size, self.storage_size)
        return path

    def test_checkpoint_restore_and_ram_only_reboot(self):
        self.boot()
        initial = self.stats()
        self.assertEqual(initial.save_interval_seconds, 60)
        self.assertEqual(initial.min_presses, 2)
        self.assertTrue(initial.persistence_enabled)
        print("Initial boot stats:", initial, flush=True)
        # Clear emulator startup GPIO transitions before the measured presses.
        request = self.template_pb2.Request()
        request.reset.SetInParent()
        self.assertEqual(
            self._heatmap_call(request).WhichOneof("response_type"), "mutation"
        )
        self.assertEqual(list(self.stats().counts), [0, 0, 0, 0])
        print("Measuring real active-low presses", flush=True)
        for _ in range(2):
            helpers.inject_heatmap_keypress(self.session, machine="central")
        self.session.mon.execute('mach set "central"')
        pending = self.stats()
        print("Pending:", pending, flush=True)
        self.assertEqual(list(pending.counts), [2, 0, 0, 0])
        self.assertEqual(pending.unsaved_presses, 2)
        # Wait for actual guest time/workqueue rather than forcing a firmware
        # save or editing its RAM. Renode may run slower than wall time.
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline:
            saved = self.stats()
            print("Checkpoint dirty:", saved.unsaved_presses, flush=True)
            if saved.unsaved_presses == 0:
                break
            time.sleep(2)
        else:
            self.fail("Automatic checkpoint did not occur within 240 seconds")
        self.assertEqual(saved.storage_error, 0)
        self.assertEqual(list(saved.counts), [2, 0, 0, 0])
        flash = self.flash_snapshot("persistent.bin")
        self.boot(flash)
        restored = self.stats()
        print("Restored:", restored, flush=True)
        self.assertEqual(list(restored.counts), [2, 0, 0, 0])
        self.assertTrue(restored.persistence_enabled)
        self.assertEqual(restored.unsaved_presses, 0)
        self.assertEqual(restored.storage_error, 0)
        request = self.template_pb2.Request()
        request.set_persistence.enabled = False
        response = self._heatmap_call(request)
        self.assertEqual(response.WhichOneof("response_type"), "mutation")
        self.assertFalse(response.mutation.persistence_enabled)
        helpers.inject_heatmap_keypress(self.session, machine="central")
        time.sleep(0.5)
        self.assertEqual(list(self.stats().counts), [3, 0, 0, 0])
        self.assertEqual(self.stats().unsaved_presses, 0)
        flash = self.flash_snapshot("ram-only.bin")
        self.boot(flash)
        fresh = self.stats()
        print("RAM reboot:", fresh, flush=True)
        self.assertFalse(fresh.persistence_enabled)
        self.assertEqual(list(fresh.counts), [0, 0, 0, 0])
        self.assertEqual(fresh.unsaved_presses, 0)
        self.assertEqual(fresh.storage_error, 0)


if __name__ == "__main__":
    unittest.main()
