"""Purpose: Run user-supplied synthetic inputs and save actual LVGL frames and logs.

The CLI builds the sanitizer host, preserves exact input JSON, runs deterministic
virtual-CAN steps and optionally opens the completed recorded player. Inputs can
vary over time through the JSON schema; the saved browser view plays the resulting
capture sequence and does not mutate a running firmware process."""
import argparse
import json
from pathlib import Path
import sys
import webbrowser

from tools.can_sim.scenario import Scenario
from .runner import ROOT, DEFAULT_CAPTURE_EVERY_MS, build_host, run_scenario


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
    arguments = parser.parse_args()
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
