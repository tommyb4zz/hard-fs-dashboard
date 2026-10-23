"""Run deterministic JavaScript regressions for recorded dashboard motion and image synchronization.

Bridge Node built-in playback regressions into unittest discovery. Node is
required rather than skipped, and any JavaScript assertion/runtime failure becomes
a Python test failure; no browser or hardware is opened by this wrapper."""
import shutil
import subprocess
import unittest
from pathlib import Path


class DashboardPlaybackTests(unittest.TestCase):
    """Require Node and propagate all JavaScript playback failures into the Python test suite."""

    def test_recorded_playback_controls_and_image_alignment(self):
        """Run Node's built-in test runner without skipping a missing runtime or failed assertion.

        Find Node on PATH, failing the assertion if missing. Invoke its built-in test
        runner on test_playback.js with a 30-second timeout and require return code zero,
        including captured stdout/stderr in any assertion failure.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        node = shutil.which("node")
        self.assertIsNotNone(node, "Node.js is required to verify dashboard playback")
        root = Path(__file__).resolve().parents[1]
        result = subprocess.run(
            [node, "--test", str(root / "tests/dashboard_playback/test_playback.js")],
            cwd=root, capture_output=True, text=True, timeout=30, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
