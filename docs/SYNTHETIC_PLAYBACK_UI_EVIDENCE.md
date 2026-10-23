# Recorded dashboard playback UI evidence

## Scope and source decisions

The playback UI changes are limited to `tools/dashboard_sim/viewer.html`,
`tools/dashboard_sim/playback.js`, `tests/dashboard_playback/test_playback.js`, and
`tests/test_dashboard_playback.py`. Python capture generation and CLI integration are owned by
the root implementation. No firmware behavior or physical CAN binding changes are part of this UI work.

- The existing viewer selected a still capture by array index. Captures already contain
  `tick_ms`, `image`, and the corresponding decoded `state`. The new clock selects the final
  recorded timestamp at or before its playhead, using elapsed milliseconds rather than counting
  animation callbacks. This preserves irregular capture intervals and playback speed across display
  refresh rates. The primary API is WHATWG's
  [animation frame provider](https://html.spec.whatwg.org/multipage/imagebitmap-and-animations.html#animation-frames).
- Candidate images load outside the visible screen. A successful image node replaces the visible
  node in the same callback that writes its associated state and capture timestamp. Every request
  has a monotonically increasing token; callbacks from superseded seeks are ignored. Image events
  follow the HTML standard's
  [image element](https://html.spec.whatwg.org/multipage/embedded-content.html#the-img-element).
- Autoplay is initially paused for a reported reduced-motion preference. The user can press Play.
  This follows the definition of the W3C
  [prefers-reduced-motion feature](https://www.w3.org/TR/mediaqueries-5/#prefers-reduced-motion).
- The generated viewer uses an inline `/* PLAYBACK_CODE */` replacement and the unchanged
  `/* CAPTURE_DATA */[]` data replacement. It has no remote script, package, or font dependency.
  The PNG files must stay beside `index.html`.

## Implemented behavior

The viewer automatically plays and repeats recordings containing more than one timestamp.
It includes Pause/Play, Replay, a millisecond time slider, speeds 0.25×/0.5×/1×/2×/4×, and a Loop
checkbox. With Loop disabled it stops at the final capture. Seeking preserves the play/pause state.
The playhead timestamp and the loaded capture timestamp are labelled separately; the visible JSON
always belongs to the successfully loaded screen image. A missing image clears both visible screen
and state, displays the path of the failed capture, and pauses at that capture's timestamp.

Empty recordings explicitly report that no captures were requested and disable motion controls.
Single-capture recordings show one screen and report that it is stationary. The desktop layout keeps
the dashboard and controls in the left column with scrollable decoded state beside them; narrower
viewports use one column. The image preserves the original 480:272 aspect ratio.

## Red/green verification

The first execution of
`.venv/bin/python -m unittest tests.test_dashboard_playback -v` failed because the required playback
module did not exist. After implementation, the same command passed. It invokes Node's built-in
test runner; a missing Node runtime fails the Python test rather than silently skipping verification.
The recorded local runtime was Node v22.18.0.

All seven deterministic JavaScript cases pass:

1. Irregular timestamp selection and loop overshoot use recorded milliseconds.
2. Pause, seek while playing, speed changes, and replay maintain intended scenario time.
3. Nonlooping completion stops on the final capture and can restart.
4. The loaded screen and visible state commit together; stale older image completion is ignored.
5. A delayed missing-image callback clears stale screen/state and pauses on the failed timestamp,
   including elapsed wall time without an animation callback.
6. Empty and single-capture recordings disable motion controls and report explicit states.
7. Reduced-motion startup is paused; DOM control events operate play, speed, seek, loop, and replay.

`git diff --check` reported no whitespace errors after these changes. Real browser behavior,
generated HTML integration, regenerated scenario capture counts, and screenshots are verified by
the root agent separately; those checks are not claimed by this focused UI report.

## Bounds

This is recorded playback of actual rendered screen captures. It does not rerun the C decoder while
the browser plays, interpolate missing pixels, edit inputs live, exercise FreeRTOS timing, or prove
physical hardware behavior. Changing input values requires editing the scenario and rerunning the
existing simulation command. Frames superseded while loading can be skipped; their state cannot
replace that of a newer successfully loaded frame.
