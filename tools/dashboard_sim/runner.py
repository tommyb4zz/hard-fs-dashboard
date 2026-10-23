"""Purpose: Execute a closed synthetic CAN loop through production dashboard code.

Build a persistent sanitizer-enabled C host, convert synthetic auxiliary state at
typed stdin boundaries, transport actual production-generated requests through
python-can virtual endpoints, and return replies to production decoding/logging/UI.
Save real 480x272 LVGL pixels as PNG, snapshots, validated binary/CSV and playback.
No physical CAN, serial device, MCU register or FreeRTOS scheduling is accessed."""
from __future__ import annotations

import json
import math
from pathlib import Path
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid
import zlib

import can

from tools.can_sim.engine import Responder, Runner
from tools.can_sim.scenario import AUXILIARY_STATUS_NAMES, Scenario
from tools.can_sim.transport import open_bus

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CAPTURE_EVERY_MS = 100


def build_host() -> Path:
    """Configure and incrementally build the production-code host executable.

    No arguments. Prefer cmake beside this Python executable, falling back to PATH;
    absence raises RuntimeError. Configure tools/dashboard_host into build/dashboard-host
    with sanitizers ON, then compile with eight jobs. Capture diagnostics and limit
    each subprocess to 180 seconds. Return the dashboard_host executable Path;
    nonzero commands raise RuntimeError and process errors/timeouts propagate.
    Build files are written, but no MCU is flashed and no bus is opened here."""
    bundled = Path(sys.executable).with_name("cmake")
    cmake = str(bundled) if bundled.exists() else shutil.which("cmake")
    if not cmake:
        raise RuntimeError("CMake is required; install it before running the dashboard host simulator")
    build = ROOT / "build/dashboard-host"
    for command in ([cmake, "-S", str(ROOT / "tools/dashboard_host"), "-B", str(build), "-DDASHBOARD_HOST_SANITIZERS=ON"],
                    [cmake, "--build", str(build), "--parallel", "8"]):
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        if result.returncode:
            raise RuntimeError(f"Host build failed:\n{result.stdout}\n{result.stderr}")
    return build / "dashboard_host"


class Host:
    """Own the sequential command boundary to the production-code host process.

    The C adapter maintains firmware-style state across ticks. Commands use
    text stdin and one JSON stdout result; stderr is a temporary file containing
    process/sanitizer diagnostics. Context exit closes and checks that process.
    """

    def __init__(self, executable: Path):
        """Start one persistent C host process and reserve its diagnostic stream.

        Executable is the compiled host Path. Open text stdin/stdout pipes and a temporary
        seekable stderr file, avoiding a full stderr pipe that could block sanitizer output.
        Store the process and error-file handles; return None. If Popen fails, close the
        error file and re-raise. Hardware peripherals are implemented by the host adapter."""
        self.errors = tempfile.TemporaryFile(mode="w+")
        try:
            self.process = subprocess.Popen([str(executable)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=self.errors, text=True, bufsize=1)
        except BaseException:
            self.errors.close()
            raise

    def __enter__(self):
        """Provide this already-started host to a with statement.

        No additional process or resource is allocated. Return self; paired __exit__ owns
        shutdown and error-file/pipe cleanup. The caller may issue commands while inside
        the context."""
        return self

    def __exit__(self, kind, value, traceback):
        """Stop the C process, release handles and report exit failure when appropriate.

        The context supplies an exception type/value/traceback or None. If C is still
        running, write/flush Q, then wait up to five seconds; broken pipe or timeout forces
        kill and wait. Close stdin/stdout and read/close the temporary error file. Return
        None so a prior exception propagates. Raise RuntimeError with diagnostics for a
        nonzero C exit only when the context was not already leaving through an exception."""
        try:
            if self.process.poll() is None:
                self.process.stdin.write("Q\n")
                self.process.stdin.flush()
            self.process.wait(timeout=5)
        except (BrokenPipeError, subprocess.TimeoutExpired):
            self.process.kill()
            self.process.wait()
        finally:
            self.process.stdin.close()
            self.process.stdout.close()
        self.errors.seek(0)
        message = self.errors.read()
        self.errors.close()
        if kind is None and self.process.returncode:
            raise RuntimeError(f"Host exited with {self.process.returncode}: {message}")

    def command(self, command: str) -> dict:
        """Send one complete stdin command and read its single JSON result.

        Command text may contain the T header and subsequent frame lines; append a final
        newline and flush before waiting. Require readable stdout within ten wall-clock
        seconds, killing the process on timeout. Parse one result line into its JSON value
        (typed here as dict). Closed pipes, EOF or malformed JSON raise RuntimeError;
        EOF diagnostics come from the stderr file. Process/pipe state changes, not physical
        hardware. Calls must be sequential because responses have no request identifiers."""
        try:
            self.process.stdin.write(command + "\n")
            self.process.stdin.flush()
        except BrokenPipeError as error:
            raise RuntimeError("Host command pipe closed") from error
        if not select.select([self.process.stdout], [], [], 10)[0]:
            self.process.kill()
            raise RuntimeError("Host command timed out")
        line = self.process.stdout.readline()
        if not line:
            self.process.wait(timeout=5)
            self.errors.seek(0)
            raise RuntimeError("Host rejected the command: " + self.errors.read())
        try:
            return json.loads(line)
        except json.JSONDecodeError as error:
            raise RuntimeError("Invalid host response: " + line) from error

    def step(self, tick_ms: int, auxiliary: dict, frames: list[can.Message]) -> dict:
        """Serialize one simulation tick, auxiliary UI state and received CAN batch.

        Tick uses integer milliseconds. Convert speed km/h to nearest nonnegative integer
        via floor(x+0.5), or -1 for unknown; map mode None/N/D/R to -1/0/1/2. Status names
        use AUXILIARY_STATUS_NAMES bit order: known sets valid, fault sets fault. Reject
        more than 128 frames before sending. Build T tick speed mode valid fault count
        followed by each frame's hex ID, extended/remote/FD flags and hex payload ('-' for
        empty data). Return the C JSON snapshot from command. Scenario validation is
        expected before this boundary; C separately checks typed range/framing limits."""
        speed = auxiliary["speed_kph"]
        speed = -1 if speed is None else math.floor(speed + 0.5)
        # -1 represents unknown at the C boundary; no vehicle bit encoding is assigned.
        mode = {None: -1, "N": 0, "D": 1, "R": 2}[auxiliary["drive_mode"]]
        valid = fault = 0
        # Bit positions are a host API contract shared with the C adapter, not CAN IDs.
        for index, name in enumerate(AUXILIARY_STATUS_NAMES):
            state = auxiliary["statuses"][name]
            if state != "unknown":
                valid |= 1 << index
            if state == "fault":
                fault |= 1 << index
        if len(frames) > 128:
            raise ValueError("A host receive batch contains at most 128 frames")
        lines = [f"T {tick_ms} {speed} {mode} {valid} {fault} {len(frames)}"]
        for frame in frames:
            lines.append(f"{frame.arbitration_id:x} {int(frame.is_extended_id)} {int(frame.is_remote_frame)} {int(frame.is_fd)} {frame.data.hex() or '-'}")
        return self.command("\n".join(lines))

    def artifact(self, command: str, path: Path) -> None:
        """Ask the C host to save one framebuffer capture or binary dump.

        Command is the host operation (P for PPM, E for eMMC stream), and path is resolved
        absolutely. Reject CR/LF in the path with ValueError so it cannot split stdin
        commands. Preserve ordinary spaces as part of the remainder of the command.
        Return None only when C replies with ok true; otherwise raise RuntimeError.
        This writes a local file through the host and performs no serial/device export."""
        value = str(path.resolve())
        if "\n" in value or "\r" in value:
            raise ValueError("Output paths must not contain line breaks")
        if not self.command(f"{command} {value}").get("ok"):
            raise RuntimeError(f"Host failed to write {path}")


def ppm_to_png(source: Path, destination: Path) -> None:
    """Convert the actual LVGL host RGB pixels into a lossless PNG file.

    Read source as binary P6 PPM with newline-separated header, maximum 255, dimensions
    exactly 480x272 and exactly three bytes per pixel. Reject unexpected headers or
    length with ValueError. Prefix each scanline with PNG filter byte zero, zlib-compress
    the unchanged RGB rows, and write signature plus IHDR/IDAT/IEND to destination.
    Return None; existing destination is replaced and filesystem errors propagate.
    No screenshot approximation, palette conversion or external image library is used."""
    data = source.read_bytes()
    signature, dimensions, maximum, pixels = data.split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if signature != b"P6" or maximum != b"255" or (width, height) != (480, 272) or len(pixels) != width * height * 3:
        raise ValueError("Unexpected host framebuffer format")

    def chunk(kind: bytes, payload: bytes) -> bytes:
        """Pack one PNG chunk with standards-format length and CRC fields.

        Kind is its four-byte chunk type and payload its raw content. Return big-endian
        32-bit payload length, kind, payload and zlib CRC over kind+payload. This nested
        helper allocates bytes only and writes no files. The PNG checksum is distinct
        from the dashboard binary-export checksums."""
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

    # PNG filter 0 carries each RGB scanline unchanged; compression is lossless.
    scanlines = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    destination.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                            + chunk(b"IDAT", zlib.compress(scanlines)) + chunk(b"IEND", b""))


def write_viewer(output: Path, captures: list[dict]) -> None:
    """Write an offline HTML player containing this run's capture/state records.

    Output is an existing directory and captures contains tick_ms/image/state dicts.
    Read the checked-in HTML template and playback.js, inline the playback code first,
    then replace the first capture placeholder with JSON whose '<' characters are
    escaped to prevent a data string from closing a script element. Relative PNG names
    keep saved playback usable beside index.html without a server. Replace index.html;
    I/O errors propagate. This only packages recorded results and does not rerun CAN."""
    template = (Path(__file__).with_name("viewer.html")).read_text()
    playback = Path(__file__).with_name("playback.js").read_text()
    records = json.dumps(captures).replace("<", "\\u003c")
    (output / "index.html").write_text(template.replace("/* PLAYBACK_CODE */", playback, 1)
                                      .replace("/* CAPTURE_DATA */[]", records, 1))


def run_scenario(scenario: Scenario, output: Path, executable: Path, *, step_ms: int = 10,
                 duration: float | None = None, capture_every_ms: int = DEFAULT_CAPTURE_EVERY_MS,
                 protected_inputs: tuple[Path, ...] = ()) -> dict:
    """Drive production CAN, LVGL and loggers through deterministic synthetic ticks.

    Scenario contains validated engineering/auxiliary tracks. Output selects replaced
    artifact paths; executable is the host binary. Validate positive finite duration
    (seconds), integer step_ms 1..1000, nonnegative capture_every_ms (0 disables images),
    32-bit rounded end tick and at most 100001 steps before opening the host. Include
    an irregular final tick. Capture eligible ticks at/after each requested interval
    and always the end; default interval is 100 ms. Reject protected input aliases,
    including existing hard links, against every planned artifact before writing.

    Open two unique same-process virtual CAN endpoints and one persistent C host.
    At each tick dispatch due replies, drain at most 128 frames, sample auxiliary
    state, then let C process frames/update UI/loggers/generate its real next request.
    Record returned C state plus exact auxiliary input in snapshots.jsonl. Send the
    C-generated request back over virtual CAN and plan its response at the same tick;
    client delivery to C occurs on a later tick. Reject updated-but-unlogged storage
    samples instead of claiming success when the 64-block fake region fills.

    Capture actual C/LVGL PPM pixels and lossless PNG at chosen ticks. Request the
    production binary export, close resources, decode header/records/footer and require
    both stored/dump CRC flags true. Write battery CSV, packaged playback and summary;
    return counts and paths. Auxiliary inputs remain in JSONL/viewer, not battery
    binary records. Filesystem/validation/process/queue/CAN errors propagate and a
    failed run may leave partial local artifacts; no hardware or FreeRTOS timing runs."""
    from dump_emmc_bin import read_dump, write_csv

    duration = scenario.duration if duration is None else duration
    if isinstance(duration, bool) or not isinstance(duration, (int, float)) or not math.isfinite(duration) or duration <= 0:
        raise ValueError("duration must be finite and positive")
    if isinstance(step_ms, bool) or not isinstance(step_ms, int) or not 1 <= step_ms <= 1000:
        raise ValueError("step-ms must be an integer within 1..1000")
    if isinstance(capture_every_ms, bool) or not isinstance(capture_every_ms, int) or capture_every_ms < 0:
        raise ValueError("capture-every-ms must be a nonnegative integer; zero disables captures")
    # Round simulation seconds once; C receives only bounded millisecond ticks.
    end_ms = math.floor(duration * 1000 + 0.5)
    if end_ms < 1 or end_ms > 0xFFFFFFFF or (end_ms + step_ms - 1) // step_ms + 1 > 100001:
        raise ValueError("Run requires a positive millisecond duration and at most 100001 steps")
    output = output.resolve()
    # Include the exact rounded end even when it is not a multiple of step_ms.
    ticks = list(range(0, end_ms, step_ms)) + [end_ms]
    capture_ticks = set()
    next_capture = 0
    for tick in ticks:
        if capture_every_ms and (tick >= next_capture or tick == end_ms):
            capture_ticks.add(tick)
            next_capture = tick + capture_every_ms
    # Enumerate every planned output before checking source path/hard-link aliases.
    artifacts = [output / name for name in
                 ("snapshots.jsonl", "log.bin", "log.csv", "index.html", "summary.json", "scenario.json")]
    artifacts.extend(output / f"frame-{tick:08d}.{suffix}" for tick in capture_ticks for suffix in ("ppm", "png"))
    for source in protected_inputs:
        for artifact in artifacts:
            if (source.resolve() == artifact.resolve() or
                    (source.exists() and artifact.exists() and source.samefile(artifact))):
                raise ValueError(f"Output artifact would overwrite the input scenario: {artifact}")
    output.mkdir(parents=True, exist_ok=True)
    channel = "dashboard-" + uuid.uuid4().hex
    captures = []
    requests = responses = 0
    with open_bus(channel=channel) as responder_bus, open_bus(channel=channel) as client, Host(executable) as host, (output / "snapshots.jsonl").open("w") as snapshots:
        runner = Runner(responder_bus, Responder(scenario))
        for tick in ticks:
            elapsed = tick / 1000
            # Dispatch delayed replies before C consumes this tick's receive batch.
            runner.step(elapsed)
            frames = []
            while len(frames) < 128:
                frame = client.recv(0)
                if frame is None:
                    break
                frames.append(frame)
            responses += len(frames)
            auxiliary = scenario.auxiliary_at(elapsed)
            record = host.step(tick, auxiliary, frames)
            record["auxiliary"] = auxiliary
            snapshots.write(json.dumps(record) + "\n")
            if record["updated"] and not record["logged"]:
                raise RuntimeError("Synthetic storage could not append the sample; the host region is 64 blocks, not the physical card")
            # Production C selects PID/cadence. Python transports that request unchanged.
            if record["request"] is not None:
                request = record["request"]
                client.send(can.Message(arbitration_id=request["id"], is_extended_id=False,
                                        data=bytes.fromhex(request["data"]), check=True))
                requests += 1
                # Plan now; the client receive batch is consumed on a following tick.
                runner.step(elapsed)
            if tick in capture_ticks:
                basename = f"frame-{tick:08d}"
                ppm = output / (basename + ".ppm")
                png = output / (basename + ".png")
                host.artifact("P", ppm)
                ppm_to_png(ppm, png)
                captures.append({"tick_ms": tick, "image": png.name, "state": record})
        host.artifact("E", output / "log.bin")
    # Validate the actual production C wire export rather than constructing Python rows.
    with (output / "log.bin").open("rb") as stream:
        header, rows = read_dump(stream)
    if not all(row["stored_crc_valid"] and row["dump_crc_valid"] for row in rows):
        raise RuntimeError("A synthetic stored/exported record failed CRC validation")
    write_csv(output / "log.csv", rows)
    write_viewer(output, captures)
    summary = {"duration_ms": end_ms, "steps": len(ticks), "requests": requests, "responses": responses,
               "exported_records": len(rows), "session_id": header["session_id"], "captures": len(captures),
               "output": str(output), "scope": "production CAN/UI/loggers with virtual CAN and fake peripherals"}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return summary
