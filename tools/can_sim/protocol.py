"""Purpose: Define byte encodings for the ten PIDs implemented by Src/app_can.c.

Signal metadata keeps engineering range/scaling next to payload width and PID.
Request recognition and response construction are pure: callers choose a virtual
transport separately. This is the repository firmware's 0x7E3/0x7EB contract;
compatibility with any actual BMS protocol remains unverified."""

from __future__ import annotations

from dataclasses import dataclass
from decimal import Decimal, ROUND_HALF_UP
import math

import can

REQUEST_ID = 0x7E3
RESPONSE_ID = 0x7EB


@dataclass(frozen=True)
class Signal:
    """Describe one implemented PID's engineering-to-wire conversion.

    pid is its two-byte identifier; width is one/two value bytes. Engineering
    values multiply by scale, round half-up and add offset before width masking.
    minimum/maximum constrain normal user inputs, not explicit raw fault bytes.
    Frozen attributes prevent reassignment; the table controls iteration order.
    """

    pid: int
    width: int
    scale: int
    offset: int
    minimum: float
    maximum: float


# Order follows app_can_request_table, not numeric PID order.
SIGNALS = {
    "pack_voltage_v": Signal(0xF00D, 2, 10, 0, 0, 6553.5),
    "pack_summed_voltage_v": Signal(0xF014, 2, 100, 0, 0, 655.35),
    "pack_current_a": Signal(0xF015, 2, 10, 32767, -3276.7, 3276.7),
    "pack_soc_pct": Signal(0xF00F, 1, 2, 0, 0, 100),
    "highest_temperature_c": Signal(0xF028, 1, 1, 0, -128, 127),
    "lowest_temperature_c": Signal(0xF029, 1, 1, 0, -128, 127),
    "average_temperature_c": Signal(0xF02A, 1, 1, 0, -128, 127),
    "lowest_cell_voltage_v": Signal(0xF032, 2, 10000, 0, 0, 6.5535),
    "highest_cell_voltage_v": Signal(0xF033, 2, 10000, 0, 0, 6.5535),
    "average_cell_voltage_v": Signal(0xF034, 2, 10000, 0, 0, 6.5535),
}
PID_SIGNALS = {definition.pid: name for name, definition in SIGNALS.items()}


def finite_number(value: object, label: str) -> float:
    """Normalize a supplied number into the float used by scenario calculations.

    Input value is unitless here; label identifies the field in error messages. Accept
    int/float only, explicitly excluding bool despite its int subclass. Convert once
    and reject overflow, NaN and infinity with ValueError. Return the finite float;
    this helper performs no clamping, mutation or transport I/O."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} must be a finite number")
    try:
        number = float(value)
    except OverflowError as error:
        raise ValueError(f"{label} must be a finite number") from error
    if not math.isfinite(number):
        raise ValueError(f"{label} must be a finite number")
    return number


def validate_value(signal: str, value: object) -> float:
    """Validate a named battery value before constructing its CAN payload.

    Signal is a key of SIGNALS; value uses that key's engineering unit (V, A, percent
    or degrees C). Resolve the table entry, require a finite number, then enforce its
    inclusive minimum/maximum. Return the float unchanged in engineering units.
    Unknown names and invalid/range-exceeding values raise ValueError; no state changes."""
    if signal not in SIGNALS:
        raise ValueError(f"unknown signal: {signal}")
    number = finite_number(value, signal)
    definition = SIGNALS[signal]
    if not definition.minimum <= number <= definition.maximum:
        raise ValueError(f"{signal} must be within {definition.minimum}..{definition.maximum}")
    return number


def encode_response(signal: str, value: float, raw_override: int | None = None) -> can.Message:
    """Construct a classic standard-CAN response for one implemented battery PID.

    Signal/value use SIGNALS engineering units. Validate value even when raw_override
    is supplied. Normally compute round-half-up(value * scale) + offset using Decimal,
    then mask to the payload width; masking represents signed temperature bytes.
    An explicit raw integer override must fit the width and allows deliberately
    unphysical wire extrema without accepting an invalid engineering input.

    Return a new eight-byte Message on 0x7EB: single-frame length, service 0x62,
    big-endian PID, one/two big-endian value bytes and zero padding. PCI length counts
    service, PID and value, excluding PCI/padding. Invalid values/overrides raise
    ValueError; message check validates framing. No bus send or shared state mutation."""
    number = validate_value(signal, value)
    definition = SIGNALS[signal]
    if raw_override is None:
        scaled = Decimal(str(number)) * definition.scale
        raw = int(scaled.quantize(Decimal("1"), rounding=ROUND_HALF_UP)) + definition.offset
        # Signed Celsius values become their two's-complement byte representation.
        raw &= (1 << (8 * definition.width)) - 1
    else:
        if isinstance(raw_override, bool) or not isinstance(raw_override, int):
            raise ValueError("raw override must be an integer")
        if not 0 <= raw_override < 1 << (8 * definition.width):
            raise ValueError(f"raw override exceeds {definition.width}-byte payload")
        raw = raw_override
    # PCI counts service (1) + PID (2) + value width, not the eight-byte padding.
    payload = bytes([3 + definition.width, 0x62, definition.pid >> 8, definition.pid & 255])
    payload += raw.to_bytes(definition.width, "big")
    payload += bytes(8 - len(payload))
    return can.Message(arbitration_id=RESPONSE_ID, is_extended_id=False, data=payload, check=True)


def make_request(signal: str) -> can.Message:
    """Construct one read request matching the firmware request table.

    Input signal must name a SIGNALS entry. Return a fresh standard eight-byte
    0x7E3 Message containing [3, 0x22, PID high, PID low, 0, 0, 0, 0]. The first
    byte describes the three service/PID bytes rather than the padded DLC. Unknown
    signals raise ValueError. This builds bytes only and does not transmit them."""
    if signal not in SIGNALS:
        raise ValueError(f"unknown signal: {signal}")
    pid = SIGNALS[signal].pid
    return can.Message(
        arbitration_id=REQUEST_ID, is_extended_id=False,
        data=bytes([3, 0x22, pid >> 8, pid & 255]) + bytes(4), check=True,
    )


def request_signal(message: can.Message) -> str | None:
    """Classify an incoming frame as one of the ten supported read requests.

    Input is a python-can Message. Require ID 0x7E3, standard classic data framing,
    DLC 4..8 equal to actual data length, PCI 3 and service 0x22 before reading PID.
    Return the table's signal name for its big-endian PID; return None for every
    unrecognized ID/type/header/PID. The length checks precede indexing. The function
    neither changes the received message nor replies on a bus."""
    if (
        message.arbitration_id != REQUEST_ID
        or message.is_extended_id or message.is_remote_frame or message.is_error_frame or message.is_fd
        or not 4 <= message.dlc <= 8 or len(message.data) != message.dlc
        or message.data[0] != 3 or message.data[1] != 0x22
    ):
        return None
    return PID_SIGNALS.get(int.from_bytes(message.data[2:4], "big"))
