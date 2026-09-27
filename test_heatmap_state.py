from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class HeatmapStateTests(unittest.TestCase):
    def test_host_core(self):
        root = Path(__file__).resolve().parent
        compiler = shutil.which("cc")
        self.assertIsNotNone(
            compiler, "A C compiler is required for heatmap state tests"
        )
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "heatmap-state-test"
            result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(root / "src"),
                    str(root / "src/typing_heatmap_state.c"),
                    str(root / "tests/heatmap_state_test.c"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS", result.stdout)
