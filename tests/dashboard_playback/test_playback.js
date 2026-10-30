/*
 * File purpose: verify recorded dashboard timing, controls, and asynchronous image/state
 * alignment.
 *
 * Node's test runner loads the actual playback module. Clock/DOM/Image stand-ins expose
 * only the used browser boundaries so tests can control callback order without real
 * timers/networking. Assertions throw on a mismatch; these tests do not exercise LVGL
 * rendering or a physical display.
 */
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const { createPlayback, mountPlayback } = require("../../tools/dashboard_sim/playback.js");

/*
 * Build a clock whose animation callbacks execute only when the test advances time.
 *
 * Inputs: none. Returns clock methods sharing wall, incrementing IDs and a Map of queued
 * callbacks. No real requestAnimationFrame or sleeps run; tests explicitly choose whether
 * to dispatch time or merely let the fake wall origin elapse.
 */
function fakeClock() {
  let wall = 0;
  let nextId = 1;
  const queued = new Map();
  return {
    /*
     * Return the deterministic browser clock in milliseconds.
     *
     * Inputs: none. Returns wall without changing it; the same origin feeds animation callback
     * timestamps.
     */
    now() { return wall; },
    /*
     * Queue one browser animation frame and return its cancellation identifier.
     *
     * Input: callback is stored in queued under the next integer ID. Returns that ID and
     * increments nextId; callback execution is deferred until advance.
     */
    requestFrame(callback) { const id = nextId++; queued.set(id, callback); return id; },
    /*
     * Remove a queued animation callback.
     *
     * Input: id is the fake callback ID. Returns undefined; deleting a missing ID is harmless
     * and does not dispatch it.
     */
    cancelFrame(id) { queued.delete(id); },
    /*
     * Advance time and execute the callbacks that were queued before this frame.
     *
     * Input: milliseconds is the explicit elapsed fake time. Returns undefined; advances wall,
     * snapshots/clears the existing queue, then dispatches each callback at the new wall
     * value. Callbacks scheduled during dispatch remain queued for the next advance,
     * preventing recursive infinite animation.
     */
    advance(milliseconds) {
      wall += milliseconds;
      const callbacks = [...queued.values()];
      queued.clear();
      for (const callback of callbacks) callback(wall);
    },
    /*
     * Let wall time pass without dispatching a browser frame, as can happen in a background
     * tab.
     *
     * Input: milliseconds is fake elapsed time. Returns undefined; changes wall only,
     * preserving queued callbacks to reproduce delayed image-error handling.
     */
    elapse(milliseconds) { wall += milliseconds; },
    /*
     * Report queued callbacks so pause behavior can be verified.
     *
     * Inputs: none. Returns Map size without mutation; zero confirms cancellation/completion
     * does not leave scheduled animation.
     */
    pending() { return queued.size; },
  };
}

/*
 * Return irregularly spaced captures with distinguishable screen-state values.
 *
 * Inputs: none. Returns a fresh four-item fixture at 0,100,450,1000 ms with different
 * filenames/speeds. It changes no external state; irregular gaps prove timestamp selection
 * rather than index-based stepping.
 */
function captures() {
  return [
    { tick_ms: 0, image: "frame-0.png", state: { speed: 0 } },
    { tick_ms: 100, image: "frame-100.png", state: { speed: 12 } },
    { tick_ms: 450, image: "frame-450.png", state: { speed: 31 } },
    { tick_ms: 1000, image: "frame-1000.png", state: { speed: 70 } },
  ];
}

/*
 * Verify selection by recorded time, including irregular intervals and loop overshoot.
 *
 * Inputs: none. Test callback returns undefined if all strict assertions succeed. It
 * advances injected time across 440/450/1000 ms and beyond end, proving floor capture
 * choice and overshoot retention; assertion failure is reported by Node.
 */
test("timestamp selection and loop use recorded milliseconds", function timestampLoop() {
  const clock = fakeClock();
  const playback = createPlayback(captures(), clock);
  playback.play();
  clock.advance(440);
  assert.equal(playback.getState().index, 1);
  clock.advance(10);
  assert.equal(playback.getState().index, 2);
  clock.advance(550);
  assert.equal(playback.getState().index, 3);
  clock.advance(120);
  assert.equal(playback.getState().position_ms, 120);
  assert.equal(playback.getState().index, 1);
});

/*
 * Verify pauses, seeks during playback, and speed changes preserve elapsed time.
 *
 * Inputs: none. Returns undefined on success; fake pause must cancel callbacks, 2x speed
 * must double subsequent time, active seek resets the wall origin and replay returns to
 * zero while playing. Any mismatch throws through the test runner.
 */
test("pause seek speed and replay maintain the intended time", function controls() {
  const clock = fakeClock();
  const playback = createPlayback(captures(), clock);
  playback.play();
  clock.advance(200);
  playback.pause();
  clock.advance(700);
  assert.equal(playback.getState().position_ms, 200);
  assert.equal(clock.pending(), 0);
  playback.seek(450);
  playback.play();
  playback.setSpeed(2);
  clock.advance(100);
  assert.equal(playback.getState().position_ms, 650);
  playback.seek(100);
  clock.advance(50);
  assert.equal(playback.getState().position_ms, 200);
  playback.replay();
  assert.equal(playback.getState().position_ms, 0);
  assert.equal(playback.getState().playing, true);
});

/*
 * Verify stopping at the end and explicitly restarting a nonlooping recording.
 *
 * Inputs: none. Returns undefined on success; advances beyond end with loop disabled,
 * checks final position/stopped/no callbacks, then verifies play restarts from zero. No
 * real browser waits are used.
 */
test("nonlooping end stays on final capture and can replay", function noLoop() {
  const clock = fakeClock();
  const playback = createPlayback(captures(), clock);
  playback.setLoop(false);
  playback.play();
  clock.advance(1400);
  assert.equal(playback.getState().position_ms, 1000);
  assert.equal(playback.getState().playing, false);
  assert.equal(clock.pending(), 0);
  playback.play();
  assert.equal(playback.getState().position_ms, 0);
  assert.equal(playback.getState().playing, true);
});

/*
 * Model the DOM boundaries used by the viewer without installing a browser emulation
 * library.
 *
 * Input: reducedMotion selects the initial preference. Returns fake clock, node Map,
 * controllable Image instances and document/window objects. It intentionally models only
 * properties/methods accessed by mountPlayback, so layout/painting/accessibility remain
 * real-browser checks.
 */
function fakeBrowser(reducedMotion = false) {
  const clock = fakeClock();
  const nodes = new Map();
  const images = [];
  class Element {
    /*
     * Create an element with the properties the viewer reads or updates.
     *
     * Input: id identifies the fake control/image. Constructor initializes mutable
     * value/checked/hidden/disabled/text/attribute/listener state; no real DOM is created and
     * no events fire during construction.
     */
    constructor(id = "") {
      this.id = id;
      this.value = "1";
      this.checked = true;
      this.hidden = false;
      this.disabled = false;
      this.textContent = "";
      this.attributes = {};
      this.listeners = new Map();
    }
    /*
     * Store a registered event callback by its DOM event name.
     *
     * Inputs: name is the event string and callback is the handler. Returns undefined; the Map
     * retains one callback per name, sufficient for the viewer's single handlers rather than
     * full DOM event semantics.
     */
    addEventListener(name, callback) { this.listeners.set(name, callback); }
    /*
     * Dispatch a control event with this element as its target.
     *
     * Input: name selects a registered handler. Returns undefined; optional chaining makes
     * absent handlers a no-op. Registered callbacks run synchronously with a minimal event
     * target object.
     */
    dispatch(name) { this.listeners.get(name)?.({ target: this }); }
    /*
     * Record ARIA and other attributes for deterministic assertions.
     *
     * Inputs: name/value are attribute data. Returns undefined; coerces value to String and
     * stores it, without implementing DOM validation or accessibility-tree updates.
     */
    setAttribute(name, value) { this.attributes[name] = String(value); }
    /*
     * Atomically replace the displayed image node with the successfully loaded candidate.
     *
     * Input: node is the loaded fake candidate with its frame ID. Returns undefined;
     * overwrites the latest node Map entry so subsequent getElementById/assertions see the
     * committed image.
     */
    replaceWith(node) { nodes.set(node.id, node); }
  }
  class ImageElement extends Element {
    /*
     * Register each requested image so tests can choose callback completion order.
     *
     * Inputs: none. Constructor calls Element initialization then appends this Image to
     * images. Assigning src does not auto-load; tests call load/fail explicitly to reproduce
     * stale asynchronous completions.
     */
    constructor() { super(); images.push(this); }
    /*
     * Trigger the image load callback at a controlled point.
     *
     * Inputs: none. Returns undefined; invokes onload only if set, synchronously. Tests can
     * load older candidates after newer ones without involving file I/O.
     */
    load() { this.onload?.(); }
    /*
     * Trigger the image error callback at a controlled point.
     *
     * Inputs: none. Returns undefined; invokes onerror only if set, allowing deterministic
     * missing-image and late-failure behavior.
     */
    fail() { this.onerror?.(); }
  }
  for (const id of ["frame", "state", "stamp", "time", "play", "replay", "speed", "loop", "playhead", "message", "error"])
    nodes.set(id, new Element(id));
  const window = {
    Image: ImageElement,
    performance: { now: clock.now },
    requestAnimationFrame: clock.requestFrame,
    cancelAnimationFrame: clock.cancelFrame,
    /*
     * Return the configured reduced-motion preference.
     *
     * Inputs: query arguments are ignored by this stand-in. Returns an object with matches
     * equal to reducedMotion; it neither observes system preferences nor dispatches preference
     * changes.
     */
    matchMedia() { return { matches: reducedMotion }; },
  };
  const document = {
    /*
     * Resolve the latest node, including any atomically replaced image.
     *
     * Input: id is a viewer element name. Returns its current Map value or undefined; no nodes
     * are created by lookup.
     */
    getElementById(id) { return nodes.get(id); },
  };
  return { clock, nodes, images, window, document };
}

/*
 * Verify autoplay and prevent asynchronous older images from replacing a newer seek.
 *
 * Inputs: none. Returns undefined if assertions pass. It starts autoplay, seeks to 450,
 * loads that image/state, then completes the obsolete first image and requires no
 * replacement. Pausing at end clears the fake scheduling chain.
 */
test("loaded image and state stay aligned across stale completions", function imageAlignment() {
  const browser = fakeBrowser();
  const playback = mountPlayback(captures(), browser.document, browser.window);
  assert.equal(playback.getState().playing, true);
  assert.equal(browser.nodes.get("state").textContent, "");
  const first = browser.images[0];
  playback.seek(450);
  const later = browser.images.at(-1);
  later.load();
  assert.equal(browser.nodes.get("frame").src, "frame-450.png");
  assert.equal(JSON.parse(browser.nodes.get("state").textContent).speed, 31);
  assert.match(browser.nodes.get("stamp").textContent, /0\.450/);
  first.load();
  assert.equal(browser.nodes.get("frame").src, "frame-450.png");
  assert.equal(JSON.parse(browser.nodes.get("state").textContent).speed, 31);
  playback.pause();
});

/*
 * Verify missing captures expose an error and never pair a stale screen with newer state.
 *
 * Inputs: none. Returns undefined on success; after loading the first image it seeks to
 * 100, elapses 800 wall ms and fails that image. It requires hidden pixels/empty
 * state/error/paused time exactly 100, then verifies a later successful seek clears the
 * error.
 */
test("missing image clears the screen and state and pauses playback", function imageFailure() {
  const browser = fakeBrowser();
  const playback = mountPlayback(captures(), browser.document, browser.window);
  browser.images[0].load();
  playback.seek(100);
  browser.clock.elapse(800);
  browser.images.at(-1).fail();
  assert.equal(browser.nodes.get("frame").hidden, true);
  assert.equal(browser.nodes.get("state").textContent, "");
  assert.match(browser.nodes.get("error").textContent, /frame-100\.png/);
  assert.equal(playback.getState().playing, false);
  assert.equal(playback.getState().position_ms, 100);
  playback.seek(450);
  browser.images.at(-1).load();
  assert.equal(browser.nodes.get("frame").hidden, false);
  assert.equal(browser.nodes.get("error").textContent, "");
});

/*
 * Verify empty and one-frame recordings have explicit stationary states.
 *
 * Inputs: none. Returns undefined on success; loops over fresh empty/single fixtures and
 * requires stopped clocks, disabled controls and explanatory messages. It proves the
 * viewer does not schedule meaningless animation for insufficient capture duration.
 */
test("empty and single recordings disable playback controls", function shortRecordings() {
  for (const items of [[], captures().slice(0, 1)]) {
    const browser = fakeBrowser();
    const playback = mountPlayback(items, browser.document, browser.window);
    assert.equal(playback.getState().playing, false);
    assert.equal(browser.nodes.get("play").disabled, true);
    assert.equal(browser.nodes.get("time").disabled, true);
    assert.match(browser.nodes.get("message").textContent, items.length ? /single capture/i : /no captures/i);
  }
});

/*
 * Verify motion preference only affects initial state and all controls remain operable.
 *
 * Inputs: none. Returns undefined on success; mounts with reduced motion, dispatches
 * play/speed/seek/loop/replay control events and checks resulting clock state. Preference
 * suppresses autoplay while manual controls continue functioning; final pause cancels fake
 * callbacks.
 */
test("reduced motion starts paused and controls dispatch playback changes", function preference() {
  const browser = fakeBrowser(true);
  const playback = mountPlayback(captures(), browser.document, browser.window);
  assert.equal(playback.getState().playing, false);
  browser.nodes.get("play").dispatch("click");
  assert.equal(playback.getState().playing, true);
  browser.nodes.get("speed").value = "0.5";
  browser.nodes.get("speed").dispatch("change");
  browser.clock.advance(200);
  assert.equal(playback.getState().position_ms, 100);
  browser.nodes.get("time").value = "700";
  browser.nodes.get("time").dispatch("input");
  assert.equal(playback.getState().position_ms, 700);
  browser.nodes.get("loop").checked = false;
  browser.nodes.get("loop").dispatch("change");
  assert.equal(playback.getState().loop, false);
  browser.nodes.get("replay").dispatch("click");
  assert.equal(playback.getState().position_ms, 0);
  playback.pause();
});

/* Screen selection must reload the same timestamp and ignore an obsolete view completion. */
test("screen changes keep one timestamp and reject older view loads", function screenSelection() {
  const browser = fakeBrowser({ reducedMotion: true });
  const Element = browser.nodes.get("speed").constructor;
  const select = new Element("view");
  browser.nodes.set("view", select);
  select.value = "main";
  const items = captures().map(item => ({...item, views: {
    main: item.image, debug: "debug-" + item.image, faults: "faults-" + item.image
  }}));
  const playback = mountPlayback(items, browser.document, browser.window);
  const old = browser.images.at(-1);
  select.value = "debug";
  select.dispatch("change");
  browser.images.at(-1).load();
  assert.equal(browser.nodes.get("frame").src, "debug-frame-0.png");
  old.load();
  assert.equal(browser.nodes.get("frame").src, "debug-frame-0.png");
  select.value = "faults";
  select.dispatch("change");
  browser.images.at(-1).load();
  assert.equal(browser.nodes.get("frame").src, "faults-frame-0.png");
  assert.equal(playback.getState().position_ms, 0);
  playback.pause();
});
