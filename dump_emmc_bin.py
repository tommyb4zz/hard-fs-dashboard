#!/usr/bin/env python3
"""Purpose: Decode production HFDB/HFDE binary logger streams and export their rows.

Offline files require no serial dependency; the optional UART path requests a dump
with ASCII b. Fixed little-endian struct definitions match the firmware wire export,
which differs from its stored 40-byte eMMC record. CRC validation covers framing
and wire records; stored-record validity is supplied by firmware flag bit 0.
CSV/XLSX retain firmware integer units so conversion remains explicit."""


import argparse
import binascii
import csv
import struct
import sys
import time
from pathlib import Path
from typing import BinaryIO

try:
    import serial
except ImportError:  # pragma: no cover
    serial = None

try:
    from openpyxl import Workbook
except ImportError:  # pragma: no cover
    Workbook = None


HEADER_MAGIC = b"HFDB"
FOOTER_MAGIC = b"HFDE"
# Little-endian wire schema: 24-byte header, 42-byte exported record, 16-byte footer.
# The exported record adds record validity and dump CRC to stored logger fields.
HEADER_STRUCT = struct.Struct("<4sHHIIHHI")
RECORD_STRUCT = struct.Struct("<I5H7hBbbbBBII")
FOOTER_STRUCT = struct.Struct("<4sIII")

RECORD_FIELDS = [
    "timestamp_ms",
    "pack_voltage_deci_v",
    "pack_summed_voltage_deci_v",
    "lowest_cell_voltage_100uv",
    "average_cell_voltage_100uv",
    "highest_cell_voltage_100uv",
    "pack_current_deci_a",
    "lowest_pack_current_deci_a",
    "average_pack_current_deci_a",
    "highest_pack_current_deci_a",
    "lowest_pack_power_kw",
    "average_pack_power_kw",
    "highest_pack_power_kw",
    "pack_soc",
    "highest_temp_c",
    "average_temp_c",
    "lowest_temp_c",
    "fault_flags",
    "record_flags",
    "stored_record_crc32",
    "dump_record_crc32",
]


def crc32_bytes(data: bytes) -> int:
    """Compute the unsigned standard CRC-32 used for binary dump framing.

    Input data is an immutable byte sequence. Return binascii.crc32(data) masked to
    32 bits; do not mutate bytes or maintain incremental state. This is used for
    header, footer and per-dump-record checks, while read_dump carries its separate
    CRC across complete wire records."""
    return binascii.crc32(data) & 0xFFFFFFFF


def parse_args() -> argparse.Namespace:
    """Parse export-source and output controls from process argv.

    Require exactly one of --port (UART device) and --input-bin (offline Path).
    Defaults: baud 115200 bits/s, output prefix emmc_dump and header wait 15 seconds.
    Return the argparse Namespace; argument errors terminate through argparse.
    This parser does not open either source or validate the binary contents."""
    parser = argparse.ArgumentParser(
        description="Request and parse the hard-fs-dashboard eMMC binary dump."
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="Serial port, e.g. /dev/cu.usbmodemXXXX")
    source.add_argument("--input-bin", type=Path, help="Decode an existing synthetic or UART capture")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument(
        "--output-prefix",
        default="emmc_dump",
        help="Output file prefix without extension",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=15.0,
        help="Seconds to wait for the binary dump header",
    )
    return parser.parse_args()


def read_exact(ser: BinaryIO, size: int) -> bytes:
    """Read a required fixed-size byte region from a binary stream.

    Ser may be a file, BytesIO or serial-style object; size is bytes. Make one read
    call and return only if its length equals size. Otherwise raise RuntimeError
    with expected/actual lengths. Stream position advances; partial reads are not
    retried or accumulated here, and underlying read errors propagate."""
    data = ser.read(size)
    if len(data) != size:
        raise RuntimeError(f"Incomplete binary dump while reading {size} bytes, got {len(data)}")
    return data


def wait_for_header(ser: BinaryIO, timeout_s: float) -> bytes:
    """Find the UART binary-export header amid preceding incoming bytes.

    Ser is a binary stream with timeout-capable reads; timeout_s uses wall-clock
    seconds measured by monotonic. Read one byte at a time, retaining only the last
    four until HFDB appears, then read the remaining fixed header bytes via read_exact.
    Return the complete header; raise RuntimeError on deadline or short header.
    Consumes preceding text/noise and changes stream position; it sends no command."""
    start = time.monotonic()
    window = bytearray()

    while (time.monotonic() - start) < timeout_s:
        chunk = ser.read(1)
        if not chunk:
            continue

        window += chunk
        if len(window) > len(HEADER_MAGIC):
            window = window[-len(HEADER_MAGIC):]

        if bytes(window) == HEADER_MAGIC:
            return HEADER_MAGIC + read_exact(ser, HEADER_STRUCT.size - len(HEADER_MAGIC))

    raise RuntimeError("Timed out waiting for binary dump header")


def unpack_header(header_bytes: bytes) -> dict:
    """Validate and decode one 24-byte little-endian HFDB stream header.

    Input bytes must match HEADER_STRUCT. Require magic HFDB, version 1, header size
    24, record size 42 and CRC over all header bytes except its last CRC field.
    Return version/session_id/record_count/flags; sizes and CRC are validation metadata.
    Wrong fields/checksum raise RuntimeError, wrong byte length may raise struct.error.
    No records are read here, and caller-owned bytes remain unchanged."""
    magic, version, header_size, session_id, record_count, record_size, flags, header_crc = (
        HEADER_STRUCT.unpack(header_bytes)
    )
    if magic != HEADER_MAGIC:
        raise RuntimeError(f"Unexpected header magic: {magic!r}")
    if version != 1:
        raise RuntimeError(f"Unsupported dump version: {version}")
    if header_size != HEADER_STRUCT.size:
        raise RuntimeError(f"Unexpected header size: {header_size}")
    if record_size != RECORD_STRUCT.size:
        raise RuntimeError(f"Unexpected record size: {record_size}")

    calculated_crc = crc32_bytes(header_bytes[:-4])
    if calculated_crc != header_crc:
        raise RuntimeError(
            f"Header CRC mismatch: expected 0x{header_crc:08X}, got 0x{calculated_crc:08X}"
        )

    return {
        "version": version,
        "session_id": session_id,
        "record_count": record_count,
        "flags": flags,
    }


def read_dump(stream: BinaryIO, raw_output: BinaryIO | None = None,
              header_bytes: bytes | None = None) -> tuple[dict, list[dict]]:
    """Read and validate an entire version-one binary export from its current position.

    Stream supplies binary bytes; optional raw_output receives each consumed header,
    record and footer verbatim. header_bytes may be the already-scanned UART header;
    otherwise read it now. Validate header first, read its declared number of 42-byte
    records, decode each row and incrementally CRC all complete wire records starting
    with binascii state zero. Read/validate the 16-byte HFDE footer and require its
    record count and stream CRC to match. Return (header, rows).

    Framing/short reads/count/stream-CRC failures raise RuntimeError; row-level stored
    and dump CRC validity are returned as flags rather than rejected here. This reader
    does not check bytes after the footer; the offline CLI does. Streams advance and
    raw_output may contain a partial capture if later validation fails."""
    if header_bytes is None:
        header_bytes = read_exact(stream, HEADER_STRUCT.size)
    header = unpack_header(header_bytes)
    if raw_output is not None:
        raw_output.write(header_bytes)
    records = []
    # binascii carries a finalized CRC between calls; zero represents C's initial all-one state.
    stream_crc = 0
    for _ in range(header["record_count"]):
        record_bytes = read_exact(stream, RECORD_STRUCT.size)
        if raw_output is not None:
            raw_output.write(record_bytes)
        # Include every exported byte, including record flags and the per-dump CRC.
        stream_crc = binascii.crc32(record_bytes, stream_crc) & 0xFFFFFFFF
        records.append(unpack_record(record_bytes))
    footer_bytes = read_exact(stream, FOOTER_STRUCT.size)
    if raw_output is not None:
        raw_output.write(footer_bytes)
    footer = unpack_footer(footer_bytes)
    if footer["record_count"] != header["record_count"]:
        raise RuntimeError(
            f"Footer record count mismatch: expected {header['record_count']}, "
            f"got {footer['record_count']}"
        )
    if footer["stream_crc"] != stream_crc:
        raise RuntimeError(
            f"Stream CRC mismatch: expected 0x{footer['stream_crc']:08X}, got 0x{stream_crc:08X}"
        )
    return header, records


def unpack_record(record_bytes: bytes) -> dict:
    """Decode one 42-byte wire record without altering firmware integer units.

    Unpack little-endian fields into RECORD_FIELDS: milliseconds, decivolts/deciamps,
    100-microvolt cell counts, integer kW, SOC, signed degrees C and flags/CRCs.
    Append dump_crc_valid from a CRC over all bytes except the final dump CRC, and
    stored_crc_valid from record_flags bit 0 (the firmware's stored-record result).
    Return the row even when either flag is false; malformed length raises struct.error.
    The Python reader does not independently recompute the stored C record's CRC."""
    values = RECORD_STRUCT.unpack(record_bytes)
    row = dict(zip(RECORD_FIELDS, values))
    payload_crc = crc32_bytes(record_bytes[:-4])
    row["dump_crc_valid"] = payload_crc == row["dump_record_crc32"]
    # Firmware checks its stored 40-byte record; Python receives that verdict in bit 0.
    row["stored_crc_valid"] = bool(row["record_flags"] & 0x01)
    return row


def unpack_footer(footer_bytes: bytes) -> dict:
    """Validate and decode the 16-byte little-endian HFDE export footer.

    Input bytes must match FOOTER_STRUCT. Require HFDE and CRC over the twelve bytes
    before footer_crc. Return record_count and stream_crc; compare those with header
    and accumulated record bytes later in read_dump. Wrong magic/checksum raises
    RuntimeError and wrong length may raise struct.error. No stream/file mutation."""
    magic, record_count, stream_crc, footer_crc = FOOTER_STRUCT.unpack(footer_bytes)
    if magic != FOOTER_MAGIC:
        raise RuntimeError(f"Unexpected footer magic: {magic!r}")

    calculated_crc = crc32_bytes(footer_bytes[:-4])
    if calculated_crc != footer_crc:
        raise RuntimeError(
            f"Footer CRC mismatch: expected 0x{footer_crc:08X}, got 0x{calculated_crc:08X}"
        )

    return {
        "record_count": record_count,
        "stream_crc": stream_crc,
    }


def write_csv(path: Path, rows: list[dict]) -> None:
    """Export decoded record rows with raw firmware units and CRC-validity columns.

    Path is the destination and rows the dictionaries returned by unpack_record.
    Replace/create a UTF-8 CSV, preserving RECORD_FIELDS column order, then append
    stored_crc_valid/dump_crc_valid. Write the header even for zero rows and return
    None. Directory/file/CSV errors propagate; engineering rescaling is not performed."""
    fieldnames = RECORD_FIELDS + ["stored_crc_valid", "dump_crc_valid"]
    with path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def write_xlsx(path: Path, rows: list[dict]) -> bool:
    """Export the same raw-unit rows to an optional Excel workbook.

    If openpyxl was unavailable at import, return False without writing a file.
    Otherwise create eMMC Dump, append the ordered field/validity headers and each
    row, save to path and return True. Existing destination may be replaced; workbook
    or filesystem errors propagate. Rows are not changed or converted into new units."""
    if Workbook is None:
        return False

    workbook = Workbook()
    sheet = workbook.active
    sheet.title = "eMMC Dump"
    headers = RECORD_FIELDS + ["stored_crc_valid", "dump_crc_valid"]
    sheet.append(headers)
    for row in rows:
        sheet.append([row[name] for name in headers])
    workbook.save(path)
    return True


def main() -> int:
    """Capture or decode a binary dump, then write raw/CSV/optional XLSX exports.

    Read CLI arguments and derive .bin/.csv/.xlsx paths from the prefix. Offline mode
    rejects input aliases (including hard links) to all outputs, reads/validates the
    stream while copying exact bytes, then rejects trailing bytes. UART mode requires
    pyserial, opens the configured device, resets buffers, waits 0.2 seconds, sends
    ASCII b and scans for HFDB before reading the export. This hardware path exists
    in this standalone utility but is not invoked by the synthetic runner/tests.

    Write tabular exports, print session/count/file locations and invalid stored/dump
    CRC row counts, then return 0. Row CRC flags are reported, not automatically fatal
    here. Argument/validation/serial/filesystem failures propagate; files written
    before a late failure can remain. No physical operation is required for --input-bin."""
    args = parse_args()
    output_prefix = Path(args.output_prefix)
    raw_path = output_prefix.with_suffix(".bin")
    csv_path = output_prefix.with_suffix(".csv")
    xlsx_path = output_prefix.with_suffix(".xlsx")

    if args.input_bin is not None:
        # Reject a self-overwrite through any export path, including existing hard links.
        for output_path in [raw_path, csv_path, xlsx_path]:
            if args.input_bin.resolve() == output_path.resolve() or (
                output_path.exists() and args.input_bin.samefile(output_path)
            ):
                raise RuntimeError("Input binary path must differ from all export output paths")
        with args.input_bin.open("rb") as source, raw_path.open("wb") as raw_file:
            header, records = read_dump(source, raw_file)
            if source.read(1):
                raise RuntimeError("Unexpected trailing bytes after binary dump footer")
    else:
        if serial is None:
            raise RuntimeError("pyserial is required for UART capture: pip install pyserial")
        with serial.Serial(args.port, args.baud, timeout=0.25) as ser:
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            time.sleep(0.2)
            ser.write(b"b")
            ser.flush()
            header_bytes = wait_for_header(ser, args.timeout)
            with raw_path.open("wb") as raw_file:
                header, records = read_dump(ser, raw_file, header_bytes=header_bytes)

    write_csv(csv_path, records)
    wrote_xlsx = write_xlsx(xlsx_path, records)

    print(f"Session ID: {header['session_id']}")
    print(f"Records:    {header['record_count']}")
    print(f"Raw dump:   {raw_path}")
    print(f"CSV:        {csv_path}")
    if wrote_xlsx:
        print(f"Excel:      {xlsx_path}")
    else:
        print("Excel:      skipped (install openpyxl to generate .xlsx)")

    invalid_stored_crc = sum(1 for row in records if not row["stored_crc_valid"])
    invalid_dump_crc = sum(1 for row in records if not row["dump_crc_valid"])
    print(f"Stored CRC invalid rows: {invalid_stored_crc}")
    print(f"Dump CRC invalid rows:   {invalid_dump_crc}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
