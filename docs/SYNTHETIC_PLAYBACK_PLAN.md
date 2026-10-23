# Moving synthetic dashboard

## Request and contract

The user ran the scenario command successfully and wants to see the screen values change with
the supplied inputs. Add automatic recorded playback of actual LVGL captures; keep editable JSON
inputs and deterministic simulation. No physical binding or firmware behavior changes.

- Default captures every 100 ms (10 frames/s with the default 10 ms simulation step).
- Viewer automatically plays and loops the saved scenario, with pause/play, replay, speed,
  loop and time scrubbing controls. Respect reduced-motion preference by starting paused.
- Select frames by their recorded millisecond timestamps, including irregular end intervals.
  Actual loaded image and shown state must identify the same frame; no invented/interpolated pixels.
- Empty/single captures, end without looping, pause, seek during playback and speed changes
  must behave explicitly. Missing images must produce a visible error instead of stale state.
- `--open` opens the completed viewer in the default browser. It does not open a physical interface.
- Inputs remain edited in JSON and require rerunning the scenario. Recorded playback is labelled.

## Bounded work

Playback UI agent: own `tools/dashboard_sim/viewer.html`, new `playback.js`, and deterministic JS
playback regressions with a Python unittest wrapper. First establish a failing playback check;
implement and test frame/time synchronization and controls; end with shared evidence in
`docs/SYNTHETIC_PLAYBACK_UI_EVIDENCE.md`. The generated HTML receives data at the existing
`/* CAPTURE_DATA */[]` marker and inlined code at `/* PLAYBACK_CODE */` so file URLs work offline.

Root: own Python viewer generation, capture defaults, CLI browser opening and integration tests;
refresh run documentation/function inventory; regenerate the user's scenario; inspect motion,
controls and frame/state alignment in the real browser. Record final results in shared evidence.
Preserve local user edits and all earlier synthetic functionality. No commits/pushes/flashing.

Review agent: independently check root integration while playback implementation is in progress,
then inspect the completed playback controls/lifecycle. Own only
`docs/SYNTHETIC_PLAYBACK_REVIEW.md`; reproduce findings in ignored scratch outputs, without
editing shared implementation. End after a bounded correctness/error-handling review.
