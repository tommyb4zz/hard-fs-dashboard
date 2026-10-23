"""Purpose: Plan fault-injected responses and dispatch them using an explicit clock.

Responder samples scenario values when requests arrive and builds independent
scheduled Message copies. Runner uses a bounded heap to deliver due responses over
an already-open bus, with stable order for equal times. These classes do not infer
wall time, electrical faults, CAN arbitration or hardware peripheral behavior."""

from __future__ import annotations

import copy
from dataclasses import dataclass
import heapq
import itertools

import can

from .protocol import encode_response, finite_number, request_signal
from .scenario import Scenario


@dataclass(frozen=True)
class ScheduledResponse:
    """Pair a planned response Message with its delivery timestamp in seconds.

    Frozen attributes protect this pair from reassignment; Message remains a
    mutable python-can object. Responder therefore deep-copies duplicate frames
    rather than sharing one mutable payload between scheduled entries.
    """

    due: float
    message: can.Message


class Responder:
    """Translate recognized requests into deterministic synthetic response plans.

    Own a Scenario and per-fault matching-request counters. Planning samples
    inputs at request time and mutates loss counters, but only Runner performs
    transport sends. No decoder, board or real BMS is simulated in this class.
    """

    def __init__(self, scenario: Scenario):
        """Store a validated Scenario and initialize this responder's request-loss state.

        Input scenario provides tracks and fault intervals. Create a fresh counter dict
        keyed by fault index; the constructor emits no frames and returns None. Counters
        belong to this instance and advance only for active matching drop faults in plan."""
        self.scenario = scenario
        self._fault_requests = {}

    def plan(self, request: can.Message, elapsed: float) -> list[ScheduledResponse]:
        """Plan response frames for a request at finite nonnegative simulation seconds.

        Ignore unrecognized request shapes by returning []. Select active faults, count
        matching drop requests and return [] for drop cadence or pause. Sample the
        engineering value at request time, so delayed delivery does not resample it.
        Combine raw override, additive delay, duplicate counts and burst counts; spacing
        is the largest active duplicate spacing. More than 4096 copies raises OverflowError.

        Encode once, mutate ID/service/PCI/PID before truncating to the smallest requested
        DLC, then deep-copy each scheduled response so copies have independent Message
        objects. Same-field mutations follow configuration order. Return due times in
        seconds and frames; validate finite times. No bus send occurs here; only drop
        counters mutate. Invalid elapsed/encoding raises ValueError."""
        elapsed = finite_number(elapsed, "elapsed")
        if elapsed < 0:
            raise ValueError("elapsed must be nonnegative")
        signal = request_signal(request)
        if signal is None:
            return []
        faults = self.scenario.active_faults(elapsed, signal)
        drop = False
        for fault in faults:
            if fault.type == "drop":
                ordinal = self._fault_requests.get(fault.index, 0) + 1
                self._fault_requests[fault.index] = ordinal
                drop |= ordinal % fault.parameters["every"] == 0
        # Count all active matching drop rules before pause/drop suppresses this reply.
        if drop or any(fault.type == "pause" for fault in faults):
            return []
        raw = None
        delay = 0
        copies = 1
        spacing = 0
        for fault in faults:
            if fault.type == "raw":
                raw = fault.parameters["value"]
            elif fault.type == "delay":
                delay += fault.parameters["seconds"]
            elif fault.type == "duplicate":
                copies += fault.parameters["count"]
                spacing = max(spacing, fault.parameters["spacing"])
            elif fault.type == "burst":
                copies += fault.parameters["count"] - 1
        if copies > 4096:
            raise OverflowError("combined response copies exceed 4096")
        message = encode_response(signal, self.scenario.values_at(elapsed)[signal], raw_override=raw)
        # Header mutations precede truncation so faults remain independent of JSON ordering.
        dlc = 8
        for fault in faults:
            parameters = fault.parameters
            if fault.type == "wrong_id":
                message.arbitration_id = parameters["id"]
            elif fault.type == "wrong_service":
                message.data[1] = parameters["service"]
            elif fault.type == "wrong_pci":
                message.data[0] = parameters["pci"]
            elif fault.type == "unknown_pid":
                message.data[2:4] = parameters["pid"].to_bytes(2, "big")
            elif fault.type == "short_dlc":
                dlc = min(dlc, parameters["dlc"])
        message.data = message.data[:dlc]
        message.dlc = dlc
        # Allocate separate Message objects so later mutations cannot couple copies.
        result = []
        for index in range(copies):
            due = finite_number(elapsed + delay + index * spacing, "response due time")
            result.append(ScheduledResponse(due, copy.deepcopy(message)))
        return result


class Runner:
    """Dispatch Responder plans through a caller-owned simulation bus.

    Keep a bounded (due, sequence, Message) heap and receive/accept/send counts.
    The explicit monotonic simulation clock makes delay/copy scheduling testable
    without sleeps; underlying bus send timeouts still use wall-clock seconds.
    """

    def __init__(self, bus: can.BusABC, responder: Responder, max_pending: int = 4096):
        """Own the dispatch state for a supplied bus and response planner.

        Bus is an already-open python-can endpoint and responder plans frames without
        transmitting. max_pending is a positive non-bool integer frame limit (default
        4096); invalid limits raise ValueError. Allocate a heap, stable tie-break sequence,
        clock initialized at zero and received/accepted/sent counters. Return None;
        bus lifetime remains the caller's responsibility."""
        if isinstance(max_pending, bool) or not isinstance(max_pending, int) or max_pending < 1:
            raise ValueError("max_pending must be a positive integer")
        self.bus = bus
        self.responder = responder
        self.max_pending = max_pending
        self._queue = []
        self._sequence = itertools.count()
        self._last_time = 0
        self.received = 0
        self.accepted = 0
        self.sent = 0

    @property
    def pending(self) -> int:
        """Report the number of queued responses not yet sent.

        Read the heap length and return a frame count. This includes both due and future
        responses, changes no queue/counter state, performs no bus access and does not
        include already transmitted frames awaiting reception at the other endpoint."""
        return len(self._queue)

    def _send_due(self, elapsed: float) -> list[can.Message]:
        """Transmit due heap entries in deterministic due-time/sequence order.

        Input elapsed is simulation seconds already validated by step. Pop at most 512
        entries whose due <= elapsed, send each with a 0.05-second bus timeout, increment
        sent after successful send and return those Messages. Future entries stay queued.
        Bus errors propagate; an entry is popped before send, so failed transmission is
        not automatically requeued. No wall-clock scheduling is performed here."""
        sent = []
        while self._queue and self._queue[0][0] <= elapsed and len(sent) < 512:
            _, _, message = heapq.heappop(self._queue)
            self.bus.send(message, timeout=0.05)
            self.sent += 1
            sent.append(message)
        return sent

    def step(self, elapsed: float) -> list[can.Message]:
        """Advance virtual response dispatch at one nondecreasing simulation timestamp.

        Require finite elapsed seconds >= the last clock (initially zero), otherwise
        raise ValueError. Send existing due entries, then receive up to 256 requests
        without waiting. Count all received frames and separately recognized requests;
        ask Responder for plans and reject queue growth beyond max_pending with
        OverflowError before inserting that request's plans. Stable sequence numbers
        order equal-time heap entries. Send newly due entries and return both sent lists.
        A step can send up to 512 frames in each of its two send phases. Clock, queue,
        planner counters and traffic counters mutate; transport errors propagate."""
        elapsed = finite_number(elapsed, "elapsed")
        if elapsed < self._last_time:
            raise ValueError("simulation clock must be nonnegative and monotonic")
        self._last_time = elapsed
        sent = self._send_due(elapsed)
        for _ in range(256):
            request = self.bus.recv(timeout=0)
            if request is None:
                break
            self.received += 1
            if request_signal(request) is not None:
                self.accepted += 1
            planned = self.responder.plan(request, elapsed)
            if self.pending + len(planned) > self.max_pending:
                raise OverflowError(f"pending response queue exceeds {self.max_pending}")
            # Sequence breaks equal-due ties without comparing mutable Message objects.
            for response in planned:
                heapq.heappush(self._queue, (response.due, next(self._sequence), response.message))
        return sent + self._send_due(elapsed)
