"""Verify the physical-board demo generator using the actual C implementation."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class BoardDemoTests(unittest.TestCase):
    def test_demo_inputs_are_fresh_bounded_and_change_over_time(self):
        """Run the real generator and RAM-only episode timeline under sanitizers."""
        root = Path(__file__).resolve().parents[1]
        compiler = shutil.which("clang") or shutil.which("cc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="board-demo-") as temporary:
            executable = Path(temporary) / "demo_tests"
            compiled = subprocess.run([
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-I" + str(root / "Inc"), str(root / "tests/demo/test_app_demo.c"),
                str(root / "Src/app_demo.c"), str(root / "Src/app_fault_history.c"),
                str(root / "Src/app_fault_monitor.c"), "-o", str(executable),
            ], capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            checked = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
