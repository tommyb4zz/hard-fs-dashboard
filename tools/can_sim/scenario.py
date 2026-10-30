"""Purpose: Parse editable synthetic battery/auxiliary timelines and timed CAN faults.

JSON validation rejects unknown/duplicate keys, invalid numbers and ambiguous
point ordering before execution. Track/Fault/Scenario hold the validated contract;
battery engineering inputs and auxiliary screen inputs stay separate. All timeline
and fault times use simulation seconds. No physical vehicle mapping is defined here."""

from __future__ import annotations

from bisect import bisect_right
from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import cast

from .protocol import PID_SIGNALS, SIGNALS, finite_number, validate_value

FAULT_PARAMETERS = {
    "pause": {},
    "drop": {"every": 1},
    "delay": {"seconds": 0.2},
    "wrong_id": {"id": 0x123},
    "wrong_service": {"service": 0x7F},
    "wrong_pci": {"pci": 0x25},
    "short_dlc": {"dlc": 3},
    "unknown_pid": {"pid": 0xFFFF},
    "duplicate": {"count": 1, "spacing": 0},
    "burst": {"count": 32},
    "raw": {"value": 0},
}

# These names are host test inputs, with no assigned vehicle CAN identifiers or electrical polarity.
AUXILIARY_STATUS_NAMES = ("imd", "fault", "rtd", "tsal", "bspd", "sdc", "ready_to_drive")


def _object(value: object, allowed: set[str], label: str) -> dict:
    """Check the shape and allowed keys of one parsed JSON object.

    Input value is arbitrary JSON, allowed is the supported key set, and label gives
    error context. Return the original dict, without copying/mutating it, only if
    it contains no unknown fields. Otherwise raise ValueError. Required fields and
    value types are checked later by the caller, not by this shape-only helper."""
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be an object")
    unknown = set(value) - allowed
    if unknown:
        raise ValueError(f"{label} has unknown fields: {sorted(unknown, key=str)}")
    return value


def _integer(value: object, minimum: int, maximum: int, label: str) -> int:
    """Parse a bounded fault parameter, including explicit-base integer strings.

    Minimum/maximum are inclusive raw integer bounds; label appears in errors.
    For strings use int(value, 0), permitting forms such as 0x123. Reject booleans,
    floats (including integral-looking floats), unsupported strings and out-of-range
    numbers with ValueError. Return an int and perform no I/O or shared mutation."""
    if isinstance(value, str):
        try:
            value = int(value, 0)
        except ValueError as error:
            raise ValueError(f"{label} must be an integer or hexadecimal string") from error
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise ValueError(f"{label} must be an integer within {minimum}..{maximum}")
    return value


def _unique_object(pairs: list[tuple[str, object]]) -> dict:
    """Build a JSON object while preserving evidence of duplicate input keys.

    json.loads supplies ordered (key, value) pairs through object_pairs_hook at every
    object level. Create a new dict, raising ValueError immediately on a repeated
    key rather than letting the later value overwrite earlier configuration.
    Return that dict; no previously returned scenario object is modified."""
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


@dataclass(frozen=True)
class Track:
    """Store an ordered input timeline independently of transport.

    Each tuple is (simulation seconds, engineering/categorical value). Parsers
    enforce strict time order and allow linear mode only for numeric values.
    A one-point step track represents a constant; endpoints hold indefinitely.
    """

    points: tuple[tuple[float, float | str | None], ...]
    interpolation: str

    def value_at(self, elapsed: float) -> float | str | None:
        """Sample this validated ordered track at elapsed simulation seconds.

        Use bisect_right so a point becomes active exactly at its timestamp. Before the
        first point return its first value; after the last return its last value. Step
        mode holds the current value. Linear mode uses the surrounding points and the
        fraction (elapsed-start)/(end-start); parsers ensure finite numerical values and
        strictly increasing times. Return a number, category or None without mutation.
        This low-level method assumes a validated track/time; Scenario sampling methods
        perform public time validation."""
        # Right insertion makes an exact point timestamp select that new value.
        index = bisect_right([point[0] for point in self.points], elapsed) - 1
        if index < 0:
            return self.points[0][1]
        start, value = self.points[index]
        if self.interpolation == "step" or index == len(self.points) - 1:
            return value
        end, next_value = self.points[index + 1]
        # Parsers permit linear interpolation only when every point is a finite number.
        value = cast(float, value)
        return value + (cast(float, next_value) - value) * (elapsed - start) / (end - start)


@dataclass(frozen=True)
class Fault:
    """Store one validated response-fault rule and its stable counter identity.

    start/end use seconds with [start, end) activation. signal None matches all
    battery requests; parameters contains type-specific normalized values.
    index distinguishes separate drop rules even when their other fields match.
    Frozen dataclass fields do not make the parameters dict deeply immutable.
    """

    index: int
    start: float
    end: float
    type: str
    signal: str | None
    parameters: dict


def _track(signal: str, configuration: object) -> Track:
    """Convert one battery constant or timeline into an immutable Track.

    Signal identifies engineering units/range through SIGNALS. A scalar becomes a
    single step point at t=0 seconds. A dict permits only interpolation and points,
    with step default or linear; each nonempty point must have finite, nonnegative,
    strictly increasing t and an in-range value. Return a tuple-backed Track.
    Invalid shape, mode, timestamp or engineering value raises ValueError; no I/O."""
    if not isinstance(configuration, dict):
        return Track(((0, validate_value(signal, configuration)),), "step")
    data = _object(configuration, {"interpolation", "points"}, signal)
    mode = data.get("interpolation", "step")
    if mode not in ("step", "linear"):
        raise ValueError(f"{signal} interpolation must be step or linear")
    points = data.get("points")
    if not isinstance(points, list) or not points:
        raise ValueError(f"{signal} points must be a nonempty array")
    parsed = []
    for point in points:
        point = _object(point, {"t", "value"}, f"{signal} point")
        timestamp = finite_number(point.get("t"), f"{signal} point t")
        value = validate_value(signal, point.get("value"))
        if timestamp < 0 or (parsed and timestamp <= parsed[-1][0]):
            raise ValueError(f"{signal} point times must be nonnegative and strictly increasing")
        parsed.append((timestamp, value))
    return Track(tuple(parsed), mode)


def _auxiliary_value(name: str, value: object) -> float | str | None:
    """Validate one synthetic screen input without assigning vehicle wire semantics.

    Speed uses km/h and accepts finite 0..65535 or None. Drive mode accepts N/D/R or
    None. Each named status accepts unknown/ok/fault. Return the validated number,
    string or None; reject booleans, unsupported types/values and unknown names with
    ValueError. These values are host UI inputs, not decoded physical CAN signals."""
    if name == "speed_kph":
        if value is None:
            return None
        number = finite_number(value, name)
        if not 0 <= number <= 65535:
            raise ValueError("speed_kph must be within 0..65535 or null")
        return number
    if name == "drive_mode":
        if value is None or (isinstance(value, str) and value in ("N", "D", "R")):
            return value
        raise ValueError("drive_mode must be N, D, R or null")
    if name in AUXILIARY_STATUS_NAMES:
        if isinstance(value, str) and value in ("unknown", "ok", "fault"):
            return value
        raise ValueError(f"{name} must be unknown, ok or fault")
    raise ValueError(f"unknown auxiliary input: {name}")


def _auxiliary_track(name: str, configuration: object) -> Track:
    """Parse one synthetic speed, mode or status track with explicit unknown values.

    Constants become a t=0 step Track. Timeline dicts permit interpolation/points
    only and require nonempty, complete, ordered points with t in seconds. Each
    point must include value even when it is None. Linear interpolation is allowed
    only for numerical speed points; mode, status and null speed use step changes.
    Return an immutable Track; malformed shapes/times/values raise ValueError."""
    if not isinstance(configuration, dict):
        return Track(((0, _auxiliary_value(name, configuration)),), "step")
    data = _object(configuration, {"interpolation", "points"}, name)
    mode = data.get("interpolation", "step")
    if mode not in ("step", "linear") or (mode == "linear" and name != "speed_kph"):
        raise ValueError(f"{name} interpolation must be step" + (" or linear" if name == "speed_kph" else ""))
    points = data.get("points")
    if not isinstance(points, list) or not points:
        raise ValueError(f"{name} points must be a nonempty array")
    parsed = []
    for point in points:
        point = _object(point, {"t", "value"}, f"{name} point")
        timestamp = finite_number(point.get("t"), f"{name} point t")
        if "value" not in point:
            raise ValueError(f"{name} point requires a value, including explicit null for unknown")
        value = _auxiliary_value(name, point["value"])
        if mode == "linear" and value is None:
            raise ValueError("linear speed_kph points cannot contain null")
        if timestamp < 0 or (parsed and timestamp <= parsed[-1][0]):
            raise ValueError(f"{name} point times must be nonnegative and strictly increasing")
        parsed.append((timestamp, value))
    return Track(tuple(parsed), mode)


def _auxiliary_tracks(configuration: object) -> dict[str, Track]:
    """Build the complete synthetic auxiliary input mapping.

    Input object permits speed_kph, drive_mode and statuses; statuses permits the
    seven AUXILIARY_STATUS_NAMES. Omitted speed/mode become None and omitted statuses
    become unknown. Return nine newly parsed Tracks, validating every supplied value
    and rejecting unknown keys with ValueError. No battery values are derived from
    these inputs and no vehicle CAN identifiers/polarities are allocated."""
    data = _object(configuration, {"speed_kph", "drive_mode", "statuses"}, "auxiliary")
    statuses = _object(data.get("statuses", {}), set(AUXILIARY_STATUS_NAMES), "auxiliary statuses")
    tracks = {name: _auxiliary_track(name, data.get(name)) for name in ("speed_kph", "drive_mode")}
    tracks.update({name: _auxiliary_track(name, statuses.get(name, "unknown")) for name in AUXILIARY_STATUS_NAMES})
    return tracks


def _auxiliary_wired(configuration: object) -> frozenset[str]:
    """Name the auxiliary inputs a scenario explicitly provides.

    Input is the already-validated auxiliary object. Return speed_kph/drive_mode when their
    keys are present and every named status, including ones whose value is "unknown" or
    null: a provided input is wired, so its missing data is a malfunction. Omitted inputs
    are unwired, like production firmware, and are never written to the fault history."""
    data = configuration if isinstance(configuration, dict) else {}
    statuses = data.get("statuses", {}) if isinstance(data.get("statuses", {}), dict) else {}
    return frozenset([name for name in ("speed_kph", "drive_mode") if name in data] + list(statuses))


def _unknown_auxiliary_tracks() -> dict[str, Track]:
    """Allocate the default auxiliary tracks for direct Scenario construction.

    Called as the dataclass default_factory with no arguments. Return fresh unknown
    speed/mode/status tracks via the same parser used for JSON inputs, so separate
    Scenario instances do not share a mutable default dict. Existing three-argument
    Scenario(duration, tracks, faults) callers retain explicit auxiliary unknowns."""
    return _auxiliary_tracks({})


def _fault(index: int, configuration: object) -> Fault:
    """Validate and normalize one timed CAN response fault.

    Index is the stable JSON-list position used for drop counters. Require a known
    type and interval 0 <= start < end in seconds; optional signal must name a
    battery PID. Accept only this type's parameter keys, fill type defaults, then
    validate seconds/spacing and bounded integer parameters. unknown_pid must be
    absent from the firmware table; raw requires a target and a width-fitting value.
    Return Fault(index, interval, type, target, parameters); invalid inputs raise
    ValueError. Interval activation itself is handled by Scenario.active_faults."""
    if (
        not isinstance(configuration, dict)
        or not isinstance(configuration.get("type"), str)
        or configuration["type"] not in FAULT_PARAMETERS
    ):
        raise ValueError("fault type is unsupported")
    kind = configuration["type"]
    data = _object(configuration, {"start", "end", "type", "signal"} | set(FAULT_PARAMETERS[kind]), "fault")
    start = finite_number(data.get("start"), "fault start")
    end = finite_number(data.get("end"), "fault end")
    if start < 0 or end <= start:
        raise ValueError("fault requires 0 <= start < end")
    signal = data.get("signal")
    if signal is not None and (not isinstance(signal, str) or signal not in SIGNALS):
        raise ValueError(f"unknown fault signal: {signal}")
    # Fill only the selected type's defaults; shape validation already rejects typos.
    parameters = {key: data.get(key, value) for key, value in FAULT_PARAMETERS[kind].items()}
    for key in ("seconds", "spacing"):
        if key in parameters:
            parameters[key] = finite_number(parameters[key], f"fault {key}")
            if parameters[key] < 0:
                raise ValueError(f"fault {key} must be nonnegative")
    bounds = {
        "every": (1, 1000000), "count": (1, 1024), "id": (0, 0x7FF),
        "service": (0, 255), "pci": (0, 255), "dlc": (0, 7), "pid": (0, 0xFFFF),
    }
    for key, (minimum, maximum) in bounds.items():
        if key in parameters:
            parameters[key] = _integer(parameters[key], minimum, maximum, f"fault {key}")
    if kind == "unknown_pid" and parameters["pid"] in PID_SIGNALS:
        raise ValueError("unknown_pid must identify a PID absent from the firmware table")
    if kind == "raw":
        if signal is None:
            raise ValueError("raw faults require a signal target")
        parameters["value"] = _integer(parameters["value"], 0, (1 << (8 * SIGNALS[signal].width)) - 1, "raw value")
    return Fault(index, start, end, kind, signal, parameters)


@dataclass(frozen=True)
class Scenario:
    """Group the complete validated simulation input contract.

    duration is seconds; tracks contains all ten battery signals, faults keeps
    JSON order, and auxiliary_tracks contains speed/mode/seven statuses. Frozen
    dataclass attributes do not deeply freeze those dictionaries; sampling
    returns new mappings so callers do not alter tracks through result mutation.
    """

    duration: float
    tracks: dict[str, Track]
    faults: tuple[Fault, ...]
    auxiliary_tracks: dict[str, Track] = field(default_factory=_unknown_auxiliary_tracks)
    auxiliary_wired: frozenset[str] = frozenset()

    @classmethod
    def from_dict(cls, configuration: object) -> Scenario:
        """Parse the version-one scenario schema into battery, auxiliary and fault tracks.

        Input is a JSON-like object with version/duration/signals/faults/auxiliary only.
        Default version is 1, duration is 10 seconds, omitted battery signals are
        synthetic engineering zero, and omitted auxiliary inputs are unknown. Require
        positive finite duration and a fault list. Enumerate faults in configuration
        order for stable counter identity, then return a new Scenario. Validation raises
        ValueError before any bus, C host or output artifact is opened."""
        data = _object(configuration, {"version", "duration", "signals", "faults", "auxiliary"}, "scenario")
        version = data.get("version", 1)
        if isinstance(version, bool) or not isinstance(version, int) or version != 1:
            raise ValueError("scenario version must be 1")
        duration = finite_number(data.get("duration", 10), "duration")
        if duration <= 0:
            raise ValueError("duration must be positive")
        signals = _object(data.get("signals", {}), set(SIGNALS), "signals")
        tracks = {name: _track(name, signals.get(name, 0)) for name in SIGNALS}
        faults = data.get("faults", [])
        if not isinstance(faults, list):
            raise ValueError("faults must be an array")
        auxiliary = data.get("auxiliary", {})
        return cls(duration, tracks, tuple(_fault(index, value) for index, value in enumerate(faults)),
                   _auxiliary_tracks(auxiliary), _auxiliary_wired(auxiliary))

    @classmethod
    def from_text(cls, text: str) -> Scenario:
        """Decode a captured JSON string and validate the complete scenario.

        Input text is the exact configuration contents, not a path. json.loads calls
        _unique_object for each JSON object, making duplicate fields fail before
        from_dict builds tracks. Return a new Scenario; malformed JSON raises the JSON
        parser's error and unsupported schema/value data raises ValueError. No file I/O."""
        return cls.from_dict(json.loads(text, object_pairs_hook=_unique_object))

    @classmethod
    def from_file(cls, path: str | Path) -> Scenario:
        """Read a user-editable UTF-8 JSON scenario from a string or Path.

        Read the whole file once and pass its text through duplicate-aware JSON parsing
        and schema validation. Return Scenario; propagate filesystem/Unicode errors and
        validation/JSON errors. This only reads configuration and never opens CAN or
        changes the source file."""
        return cls.from_text(Path(path).read_text(encoding="utf-8"))

    def values_at(self, elapsed: float) -> dict[str, float]:
        """Sample every configured battery track at simulation seconds.

        Require elapsed to be a finite nonnegative JSON number, rejecting bool/string
        and invalid times with ValueError. Return a new name-to-float dict in SIGNALS
        engineering units using each Track's endpoint/step/linear behavior. No transport,
        logging or track mutation occurs; omitted configured signals remain synthetic zero."""
        elapsed = finite_number(elapsed, "elapsed")
        if elapsed < 0:
            raise ValueError("elapsed must be nonnegative")
        return {name: cast(float, track.value_at(elapsed)) for name, track in self.tracks.items()}

    def auxiliary_at(self, elapsed: float) -> dict:
        """Sample synthetic screen inputs at a finite nonnegative time in seconds.

        Validate elapsed before looking up tracks. Return a fresh dict with speed_kph,
        drive_mode and a new statuses dict for all seven names. Values preserve None and
        unknown explicitly; speed is still numerical km/h here and host conversion rounds
        later. Invalid time raises ValueError. Changing the returned mapping does not
        change tracks, and this method performs no CAN or electrical interpretation."""
        elapsed = finite_number(elapsed, "elapsed")
        if elapsed < 0:
            raise ValueError("elapsed must be nonnegative")
        return {
            "speed_kph": self.auxiliary_tracks["speed_kph"].value_at(elapsed),
            "drive_mode": self.auxiliary_tracks["drive_mode"].value_at(elapsed),
            "statuses": {name: self.auxiliary_tracks[name].value_at(elapsed) for name in AUXILIARY_STATUS_NAMES},
        }

    def active_faults(self, elapsed: float, signal: str) -> tuple[Fault, ...]:
        """Select configured faults applying to one signal at a supplied time.

        Elapsed uses simulation seconds and signal is the request's battery name.
        Return a tuple in original configuration order when start <= elapsed < end and
        target is either None (all signals) or the requested name. No counters advance
        here; Responder.plan counts matching drop requests. This selector assumes the
        caller already validated elapsed/name and performs no I/O or mutation."""
        return tuple(fault for fault in self.faults if fault.start <= elapsed < fault.end and fault.signal in (None, signal))
