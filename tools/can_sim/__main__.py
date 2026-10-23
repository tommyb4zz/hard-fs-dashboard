"""Purpose: Provide the standalone virtual CAN responder/demo command.

Load user JSON, open only supported simulation endpoints, scale wall-clock time
into simulation seconds and emit JSON-line traffic evidence. Optional --demo sends
cyclic requests inside this process because python-can virtual channels do not
connect separate processes. This command does not render the LVGL dashboard."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import sys
import time

import can

from .engine import Responder, Runner
from .protocol import SIGNALS, finite_number, make_request
from .scenario import Scenario
from .transport import open_bus


def _positive(value: str) -> float:
    """Parse a CLI numeric string into a finite positive float.

    The caller determines units: duration/request period are simulation seconds and
    rate is simulation seconds per wall-clock second. Reject zero/negative, overflow,
    NaN, infinity and malformed numbers as argparse.ArgumentTypeError. Return the
    float; parsing has no bus or filesystem effects."""
    try:
        number = finite_number(float(value), "argument")
    except (ValueError, OverflowError) as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    if number <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return number


def _log_frame(event: str, elapsed: float, message: can.Message) -> None:
    """Write a frame event as one flushed JSON line to stdout.

    Event labels the direction, elapsed is simulation seconds rounded to six decimal
    places, and message supplies arbitration ID, actual DLC/payload and frame flags.
    Return None after print; this records bytes already sent/received and does not
    send another frame. stdout write errors propagate."""
    print(json.dumps({
        "event": event, "t": round(elapsed, 6),
        "id": f"0x{message.arbitration_id:03X}", "dlc": message.dlc,
        "data": message.data.hex(), "extended": message.is_extended_id, "fd": message.is_fd,
    }), flush=True)


def run(arguments: argparse.Namespace) -> dict:
    """Execute the virtual responder using a wall-clock-paced simulation timeline.

    Arguments select scenario, optional duration override, transport/channel, rate
    and optional demo request period. Open a responder endpoint and, for demo mode,
    a same-channel client in this process; ExitStack closes both on exit. Scale
    monotonic wall time by rate. Demo cycles the ten PID requests and skips missed
    periods instead of flooding catch-up requests. Step the responder, print TX/RX
    JSON events, drain at most 1024 demo replies per loop, and sleep briefly.

    Stop once simulation duration is reached, then print/return traffic summary
    including pending unsent frames. This CLI is paced rather than the deterministic
    millisecond C host runner. Configuration, transport and I/O errors propagate;
    pending delayed replies are not force-flushed after the stop time."""
    scenario = Scenario.from_file(arguments.scenario) if arguments.scenario else Scenario.from_dict({})
    duration = arguments.duration if arguments.duration is not None else scenario.duration
    channel = arguments.channel or ("vcan0" if arguments.interface == "socketcan" else "dashboard-simulation")
    with ExitStack() as resources:
        bus = resources.enter_context(open_bus(arguments.interface, channel))
        client = resources.enter_context(open_bus(arguments.interface, channel)) if arguments.demo else None
        runner = Runner(bus, Responder(scenario))
        names = tuple(SIGNALS)
        request_index = 0
        next_request = 0
        start = time.monotonic()
        while True:
            elapsed = (time.monotonic() - start) * arguments.rate
            if elapsed >= duration:
                break
            if client is not None and elapsed >= next_request:
                request = make_request(names[request_index % len(names)])
                client.send(request, timeout=0.05)
                _log_frame("demo_tx", elapsed, request)
                request_index += 1
                # Skip missed periods instead of producing an accidental catch-up flood.
                next_request = elapsed + arguments.request_period
            for response in runner.step(elapsed):
                _log_frame("response_tx", elapsed, response)
            if client is not None:
                for _ in range(1024):
                    response = client.recv(timeout=0)
                    if response is None:
                        break
                    _log_frame("demo_rx", elapsed, response)
            time.sleep(min(0.005, arguments.request_period / arguments.rate / 2))
        summary = {
            "event": "summary", "interface": arguments.interface, "channel": channel,
            "duration": duration, "rate": arguments.rate, "received": runner.received,
            "accepted": runner.accepted, "sent": runner.sent, "pending_at_stop": runner.pending,
        }
    print(json.dumps(summary), flush=True)
    return summary


def main() -> int:
    """Parse CAN-simulator arguments and map execution failures to process exit codes.

    Read argv with argparse; defaults are virtual backend, rate 1 and demo request
    period 0.05 simulation seconds. --demo is required to originate built-in request
    traffic. Call run, returning 0 on success, 130 for KeyboardInterrupt, or 2 after
    printing validation/I/O/CAN/queue failures to stderr. Argument errors are handled
    by argparse. This entry point never permits a physical CAN backend."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", type=Path, help="editable JSON scenario; omitted signals default to synthetic zero")
    parser.add_argument("--interface", choices=("virtual", "socketcan"), default="virtual")
    parser.add_argument("--channel", help="virtual channel name, or verified Linux vcan0/vcan1/etc.")
    parser.add_argument("--demo", action="store_true", help="send ten cyclic requests using a client in this process")
    parser.add_argument("--duration", type=_positive, help="stop after this many simulation seconds")
    parser.add_argument("--rate", type=_positive, default=1, help="simulation seconds per wall-clock second")
    parser.add_argument("--request-period", type=_positive, default=0.05, help="demo request spacing in simulation seconds")
    arguments = parser.parse_args()
    try:
        run(arguments)
    except KeyboardInterrupt:
        return 130
    except (ValueError, OSError, can.CanError, OverflowError) as error:
        print(f"can_sim: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
