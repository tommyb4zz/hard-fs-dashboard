"""Purpose: Run user-supplied synthetic inputs and save actual LVGL frames and logs.

The CLI builds the sanitizer host, preserves exact input JSON, runs deterministic
virtual-CAN steps and optionally opens the completed recorded player. Inputs can
vary over time through the JSON schema; the saved browser view plays the resulting
capture sequence and does not mutate a running firmware process. --live instead runs
the same production code in real time behind a local page that accepts touches."""
import argparse
import json
from pathlib import Path
import sys
import webbrowser

from tools.can_sim.scenario import Scenario
from .live import LiveDashboard, make_server
from .runner import ROOT, DEFAULT_CAPTURE_EVERY_MS, build_host, run_scenario


def run_live(scenario: Scenario, port: int, open_browser: bool, storage: Path | None) -> int:
    """Serve the running dashboard on 127.0.0.1:port until interrupted; return 0.

    Build the sanitizer host, start real-time stepping and print the page URL. Ctrl+C
    stops the server, then the host (its exit status is checked) and the virtual buses."""
    dashboard = LiveDashboard(build_host(), scenario, storage)
    server = make_server(dashboard, port)  # Bind first: a busy port fails before the host starts.
    try:
        dashboard.start()
    except BaseException:
        server.server_close()
        raise
    url = f"http://127.0.0.1:{server.server_address[1]}/"
    print(f"Live dashboard running at {url} (Ctrl+C to stop)", flush=True)
    if open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        dashboard.stop()
    return 0


def main() -> int:
    """Run a JSON scenario into local captures/logs and optionally open its player.

    Parse argv for scenario/output, optional seconds override, step milliseconds,
    capture interval (100 ms default, 0 off) and explicit --open. Read exact scenario
    bytes once, validate their UTF-8 JSON, build the sanitizer host and run the closed
    virtual loop with the source protected from output overwrite. Save those same bytes
    as scenario.json and print the returned summary. I/O/validation/runtime errors
    exit through argparse with status 1. On success return 0.

    Only --open calls the OS default-browser launcher for the completed index.html file
    URI; a false result or browser/OSError prints its saved path while retaining a
    successful run. No editing occurs inside playback; changed input JSON requires
    rerunning this entry point."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", type=Path, default=ROOT / "tools/can_sim/scenarios/dashboard.json")
    parser.add_argument("--output", type=Path, default=ROOT / "build/dashboard-sim")
    parser.add_argument("--duration", type=float, help="override scenario seconds")
    parser.add_argument("--step-ms", type=int, default=10, help="deterministic clock increment, 1..1000")
    parser.add_argument("--capture-every-ms", type=int, default=DEFAULT_CAPTURE_EVERY_MS,
                        help="capture interval, default 100 ms; 0 disables images")
    parser.add_argument("--open", action="store_true", help="open the completed animated viewer in the default browser")
    parser.add_argument("--live", action="store_true",
                        help="run the dashboard in real time behind an interactive local page instead of recording")
    parser.add_argument("--port", type=int, default=8765, help="--live page port on 127.0.0.1 (0 picks a free one)")
    parser.add_argument("--storage-file", type=Path,
                        help="--live fake eMMC card file kept between runs; never a device or unrelated file")
    arguments = parser.parse_args()
    if arguments.live:
        try:
            scenario = Scenario.from_text(arguments.scenario.read_text())
            return run_live(scenario, arguments.port, arguments.open, arguments.storage_file)
        except (OSError, ValueError, RuntimeError) as error:
            parser.exit(1, f"{error}\n")
    try:
        # Preserve exactly the file that was parsed, not a later reread or reserialized JSON.
        scenario_bytes = arguments.scenario.read_bytes()
        scenario = Scenario.from_text(scenario_bytes.decode("utf-8"))
        summary = run_scenario(scenario, arguments.output, build_host(), step_ms=arguments.step_ms,
                               duration=arguments.duration, capture_every_ms=arguments.capture_every_ms,
                               protected_inputs=(arguments.scenario,))
        (arguments.output / "scenario.json").write_bytes(scenario_bytes)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(summary, indent=2))
    # Browser launch follows successful generation and is explicitly requested by the CLI.
    if arguments.open:
        viewer = arguments.output.resolve() / "index.html"
        try:
            opened = webbrowser.open(viewer.as_uri())
        except (webbrowser.Error, OSError):
            opened = False
        if not opened:
            print(f"Browser did not open. Open the saved viewer: {viewer}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
