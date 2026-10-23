"""Verify browser opening is explicit and points to the completed synthetic viewer.

Mock completed host execution and the default-browser launcher to test CLI
orchestration in isolation. Verify exact input preservation, space-safe file URLs,
explicit browser opt-in and nonfatal launcher failure without real browser effects."""
import contextlib
import io
import json
import tempfile
import unittest
import webbrowser
from pathlib import Path
from unittest.mock import patch

from tools.dashboard_sim import __main__ as command


class DashboardCliTests(unittest.TestCase):
    """Check user-facing launch behavior without opening a browser during automated tests."""

    def run_command(self, directory, extra, browser_result=True):
        """Run CLI orchestration against a completed-output boundary and capture its public result.

        Directory owns temporary artifacts; extra adds CLI flags and browser_result
        selects launcher success/failure/exception. Mock host build/run and OS browser,
        capture stdout/stderr, require exit zero and exact preserved JSON bytes. Return
        (output Path, browser mock, stderr text). No real browser or host process starts."""
        output = Path(directory) / "run with spaces"
        output.mkdir()
        source = Path(directory) / "input.json"
        source.write_text('{"duration": 0.2}')
        (output / "index.html").write_text("<!doctype html><title>Dashboard</title>")
        summary = {"output": str(output), "captures": 3}
        stdout, stderr = io.StringIO(), io.StringIO()
        with patch("sys.argv", ["dashboard_sim", "--scenario", str(source), "--output", str(output), *extra]), \
                patch.object(command, "build_host", return_value=Path("host")), \
                patch.object(command, "run_scenario", return_value=summary), \
                patch("webbrowser.open", return_value=browser_result,
                      side_effect=browser_result if isinstance(browser_result, Exception) else None) as browser, \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = command.main()
        self.assertEqual(result, 0)
        self.assertEqual(json.loads(stdout.getvalue()), summary)
        self.assertEqual((output / "scenario.json").read_bytes(), source.read_bytes())
        return output, browser, stderr.getvalue()

    def test_open_launches_the_completed_viewer_uri(self):
        """An explicit --open request must launch the completed HTML at a space-safe file URI.

        Use the mocked completed run with a directory containing spaces and --open.
        Require exactly one browser call with the resolved file URI and no error output.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with tempfile.TemporaryDirectory() as directory:
            output, browser, error = self.run_command(directory, ["--open"])
            browser.assert_called_once_with((output / "index.html").resolve().as_uri())
            self.assertEqual(error, "")

    def test_default_run_keeps_browser_launch_optional(self):
        """CLI automation without --open must retain its JSON output without a browser side effect.

        Run the mocked CLI without --open. Assert no browser call and empty stderr;
        JSON summary and exact scenario-byte preservation are checked by the helper.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with tempfile.TemporaryDirectory() as directory:
            _, browser, error = self.run_command(directory, [])
            browser.assert_not_called()
            self.assertEqual(error, "")

    def test_failed_browser_launch_preserves_completed_artifacts(self):
        """An unavailable default browser must expose the saved viewer location without losing output.

        Make webbrowser.open return False for --open. Assert it was attempted once
        and stderr identifies the saved index.html while successful outputs remain.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        with tempfile.TemporaryDirectory() as directory:
            output, browser, error = self.run_command(directory, ["--open"], browser_result=False)
            browser.assert_called_once()
            self.assertIn(str(output / "index.html"), error)

    def test_browser_error_reports_the_saved_viewer(self):
        """Browser registry or process errors must retain the generated run and give its path.

        Inject webbrowser.Error and OSError separately. The CLI must return success
        for completed data and print the saved player path rather than lose the run.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        for failure in (webbrowser.Error("unavailable"), OSError("browser executable missing")):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                output, _, error = self.run_command(directory, ["--open"], browser_result=failure)
                self.assertIn(str(output / "index.html"), error)


if __name__ == "__main__":
    unittest.main()
