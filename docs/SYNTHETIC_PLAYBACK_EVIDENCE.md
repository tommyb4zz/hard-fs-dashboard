# Moving dashboard verification

## Source and decision record

The user's command successfully generated eleven captures for a ten-second scenario. Inspected
`viewer.html`, `runner.py` and `__main__.py`: the viewer only had a manual capture slider and the
capture default was 1000 ms. The requested visible movement requires automatic timestamp-based
playback and more frequent actual screen captures.

The bounded plan is [SYNTHETIC_PLAYBACK_PLAN.md](SYNTHETIC_PLAYBACK_PLAN.md). The solution retains
the production LVGL framebuffer output and uses 100 ms captures by default, with replay controls.
It does not interpolate invented pixels or rerun the CAN pipeline in the browser. Changes to
inputs still require editing JSON and rerunning the deterministic generator.

The Python standard-library `webbrowser.open` source was inspected locally: it dispatches to the
default registered browser and reports success as a boolean. `--open` is explicit so automation
without the flag retains its existing JSON-only CLI behavior. Browser failure leaves artifacts
available and prints their viewer path. The actual opening interaction during verification uses
the available Codex browser-control tools, not shell-driven UI automation.

## Regression evidence

Before implementation, the root's new capture/CLI tests reproduced two unrecognized `--open`
errors and a capture-count failure: a 350 ms run produced two captures, rather than the required
0/100/200/300/350 ms five captures. CLI launch tests subsequently passed with browser calls
replaced at the external boundary; no automated test opens a real browser. Tests include paths
with spaces, no launch without `--open`, false return, and browser registry/process errors.

Playback-agent red/green timing, image-load race and control tests are recorded separately in
[SYNTHETIC_PLAYBACK_UI_EVIDENCE.md](SYNTHETIC_PLAYBACK_UI_EVIDENCE.md). Independent review is in
[SYNTHETIC_PLAYBACK_REVIEW.md](SYNTHETIC_PLAYBACK_REVIEW.md).

## Integration and real browser

- The full Python suite passed **88 tests**, including the wrapper that executes all **seven
  JavaScript playback tests**. Pre-commit checks and `git diff --check` passed.
- The generated source inventory check passed with **484 function definitions and 365 edges**.
  JavaScript functions have source comments and the separate inspected diagram in
  [FUNCTION_FLOW.md](FUNCTION_FLOW.md#recorded-screen-movement).
- Regenerated `tools/can_sim/scenarios/user-inputs.json` with the 100 ms default: **10,000 ms,
  1001 steps, 188 requests, 177 responses, 177 validated exported records, and 101 captures**.
  `build/dashboard-sim/scenario.json` matches the supplied input file byte for byte, and the
  generated HTML embeds the final playback source.
- In the actual Codex browser at `http://127.0.0.1:8866/index.html`, autoplay advanced the
  capture timestamp and speed label. Pausing and seeking to **4500 ms** selected
  `frame-00004500.png`, a loaded **480×272** image, matching `tick_ms: 4500`, speed **90**,
  drive mode **D**, and the supplied IMD fault.
- Selecting **2×**, disabling loop and playing from 9950 ms stopped at the final **10,000 ms**
  capture. Speed was restored to **1×** and looping was enabled for the delivered viewer.
  The inspected browser console returned no warnings or errors.
- A full-page screenshot of the paused 4500 ms image and its controls is saved at
  `build/dashboard-sim/playback-verified.jpg`. The delivered browser tab resumes playback.

No physical hardware or firmware behavior is changed by the recorded-playback feature.
