"""Purpose: Open simulation CAN endpoints and reject physical CAN selection.

The portable backend is python-can's same-process virtual bus. Linux SocketCAN
is allowed only after sysfs and iproute2 independently identify the selected
vcan device. External python-can configuration is ignored. Interface creation
and physical adapter/BMS integration remain outside this module."""

from __future__ import annotations

import json
from pathlib import Path
import re
import subprocess
import sys

import can


def validate_vcan(channel: str, *, sysfs_root: Path = Path("/sys"), platform: str = sys.platform) -> None:
    """Verify that a requested SocketCAN interface is a Linux software vcan device.

    Channel must match vcan followed by digits. platform/sysfs_root can be injected
    by tests; production defaults use this OS and /sys. Require the class/net symlink
    to resolve to devices/virtual/net/channel, ARPHRD_CAN type text 280 and no device
    node. Then run ip -details -json link show dev with a five-second timeout and
    require exactly one matching interface with info_kind vcan. Return None on
    success. Failures raise ValueError, including missing/malformed kernel evidence.
    This reads metadata and runs ip; it never creates an interface or sends a frame."""
    if not isinstance(channel, str) or re.fullmatch(r"vcan[0-9]+", channel) is None:
        raise ValueError("SocketCAN simulation accepts only explicitly named vcan interfaces, such as vcan0")
    if platform != "linux":
        raise ValueError("SocketCAN vcan requires Linux; use the virtual backend on this host")
    try:
        device = (sysfs_root / "class" / "net" / channel).resolve(strict=True)
        expected = (sysfs_root / "devices" / "virtual" / "net" / channel).resolve(strict=True)
        if device != expected or (device / "type").read_text().strip() != "280" or (device / "device").exists():
            raise ValueError("interface lacks virtual CAN sysfs evidence")
        result = subprocess.run(
            ["ip", "-details", "-json", "link", "show", "dev", channel],
            check=True, capture_output=True, text=True, timeout=5,
        )
        links = json.loads(result.stdout)
        if (
            not isinstance(links, list) or len(links) != 1
            or links[0].get("ifname") != channel
            or links[0].get("linkinfo", {}).get("info_kind") != "vcan"
        ):
            raise ValueError("kernel interface kind must be vcan")
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError, AttributeError) as error:
        raise ValueError(f"cannot verify {channel} as vcan: {error}") from error


def open_bus(interface: str = "virtual", channel: str = "dashboard-simulation") -> can.BusABC:
    """Open one supported simulation endpoint while ignoring external CAN configuration.

    Channel is a nonempty string of at most 128 characters. virtual creates a
    same-process python-can endpoint with a 1024-frame receive queue and no own-message
    reception. socketcan first verifies Linux vcan, then opens that software endpoint.
    Return BusABC; caller must close/shutdown it. Unsupported backend/channel metadata
    raises ValueError and bus-open errors propagate. Physical adapters cannot be
    selected through this function; environment/user CAN settings are ignored."""
    if not isinstance(channel, str) or not channel or len(channel) > 128:
        raise ValueError("channel must be a nonempty string of at most 128 characters")
    if interface == "virtual":
        return can.Bus(
            interface="virtual", channel=channel, ignore_config=True,
            receive_own_messages=False, rx_queue_size=1024,
        )
    if interface == "socketcan":
        validate_vcan(channel)
        return can.Bus(
            interface="socketcan", channel=channel, ignore_config=True,
            receive_own_messages=False,
        )
    raise ValueError("only virtual and verified SocketCAN vcan interfaces are supported")
