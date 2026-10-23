# Independent synthetic playback review

## Scope and disposition

Reviewed the contract in `SYNTHETIC_PLAYBACK_PLAN.md`, Python capture/viewer generation and CLI,
the recorded-playback clock, browser bindings, and their regression tests. No actionable finding
remains within this bounded review. Actual browser motion and appearance are verified separately
by the root agent; this review does not establish physical hardware or FreeRTOS behavior.

## Source checks

- `tools/dashboard_sim/runner.py:28` sets the default capture interval to 100 ms; the CLI and
  `run_scenario()` use that constant. Capture selection includes the final simulation timestamp,
  including an irregular final interval.
- `runner.py:147` embeds both playback code and recorded data in the generated HTML. JSON `<`
  characters are escaped before insertion into the script. The generated viewer does not require
  an external JavaScript file; PNG files remain relative to the viewer.
- `tools/dashboard_sim/__main__.py:33` opens the completed viewer only for `--open`, using its
  resolved file URI. Browser failure reports the saved path after the simulation has completed.
- `tools/dashboard_sim/playback.js:46` advances by elapsed wall time and the selected speed.
  Captures are selected by timestamp. Pause cancels the queued animation callback, seeks preserve
  play/pause state, and nonlooping playback stops at the final timestamp.
- `playback.js:162` commits an already loaded image with that capture's state and timestamp.
  Request-version checks reject superseded load/error callbacks. The error handler pins the
  failed capture before pausing, clears the image/state pair, and displays the failed filename.
- Empty and single-frame recordings disable motion controls. Reduced-motion preference starts
  the viewer paused. Supplied state is displayed through `textContent`.

## Executed verification

| Check | Result |
|---|---|
| `unittest discover -s tests -p 'test_dashboard_cli.py' -v` | Four tests passed; explicit launch, optional launch, false launch result, and browser error |
| `unittest discover -s tests -p 'test_dashboard_sim.py' -v` | Seven tests passed, including default 100 ms captures and the 350 ms final frame |
| `node --test tests/dashboard_playback/test_playback.js` | Seven tests passed: irregular timing/loop, pause/seek/speed/replay, nonlooping completion, asynchronous image/state alignment, image failure, short recordings, and reduced motion/controls |
| Additional ignored-copy late-error test | Passed after 400 ms elapsed without an animation callback; error remains visible and playback pauses |
| Generated offline HTML probe | Exactly one inline script, both template markers replaced, closing-script input escaped; `node --check` passed |

The independent probes and mutation copies are in ignored `build/playback-review/`.
No implementation file was changed by this review.

## Mutation evidence

- In an isolated CLI copy, inverting `if arguments.open` caused all three CLI cases present at
  that point to fail. The real source was untouched.
- In an isolated playback copy, removing the load callback's version guard caused
  `loaded image and state stay aligned across stale completions` to fail: the obsolete
  `frame-0.png` replaced `frame-450.png`. The remaining six cases passed.

These checks demonstrate coverage of the two changed behaviors; they are not an exhaustive
proof of playback correctness or browser compatibility.
