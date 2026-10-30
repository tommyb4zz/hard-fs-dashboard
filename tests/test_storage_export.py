"""Purpose: Verify production C logger captures, repeat flushes, and Python export CRC validation.

Compile the RAM-backed production C logger adapter under sanitizers, then load
the real Python exporter. Cover block tails/capacity/reset/retries, framing/CRC
corruption, signed units and offline/fake-UART CLIs without a physical serial device."""

import csv
import importlib.util
import io
import shutil
import struct
import subprocess
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch


class StorageExportTests(unittest.TestCase):
    """Run the production logger under sanitizers and inspect its real HFDB byte stream."""

    @classmethod
    def setUpClass(cls):
        """Compile the RAM-backed C adapter and load the actual Python export script.

        Locate clang/cc, compile the production storage adapter with strict warnings
        and ASan/UBSan into a TemporaryDirectory, then load dump_emmc_bin.py as the actual
        export module. Store both on the class; compiler/import failures propagate."""
        cls.root = Path(__file__).resolve().parents[1]
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            raise RuntimeError("A host C compiler is required for storage integration tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="dashboard-storage-")
        cls.executable = Path(cls.temporary.name) / "emmc_replay"
        subprocess.run([
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
            "-I" + str(cls.root / "tests/storage_host/include"),
            "-I" + str(cls.root / "Inc"),
            str(cls.root / "tests/storage_host/test_app_emmc_log.c"),
            "-o", str(cls.executable),
        ], check=True, capture_output=True, text=True)
        spec = importlib.util.spec_from_file_location("storage_dump", cls.root / "dump_emmc_bin.py")
        cls.export = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.export)

    @classmethod
    def tearDownClass(cls):
        """Remove the sanitizer executable and all temporary captures after the suite.

        Delete the class-owned temporary directory containing compiled adapter and
        export captures after all cases. Return None; no physical storage is accessed."""
        cls.temporary.cleanup()

    def capture(self, mode):
        """Run one production-C scenario and return its stdout binary capture.

        Mode names one scenario supported by the compiled C adapter. Run it with a
        ten-second timeout, require successful process exit and return stdout bytes.
        Subprocess failures propagate, including sanitizer failures; no UART device opens."""
        process = subprocess.run([str(self.executable), mode], check=True, capture_output=True,
                                 timeout=10)
        return process.stdout

    def unpack_rows(self, capture):
        """Decode the fixed wire records independently of the stream-validation path.

        Input capture is the adapter binary byte stream. Validate its first 24-byte
        header and require total length 24 + count*42 + 16. Return decoded rows at fixed
        42-byte offsets; this helper intentionally does not check the aggregate stream CRC."""
        header = self.export.unpack_header(capture[:24])
        self.assertEqual(len(capture), 24 + header["record_count"] * 42 + 16)
        return [self.export.unpack_record(capture[offset:offset + 42])
                for offset in range(24, len(capture) - 16, 42)]

    def test_repeated_partial_flush_preserves_both_samples(self):
        """Flushing sample 11 before sample 22 must not insert a padded slot between them.

        Decode the C partial scenario; require timestamps 11/22, voltages 4011/4022
        decivolts and valid stored CRC flags, excluding padded gaps after repeated flushes.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        rows = self.unpack_rows(self.capture("partial"))
        self.assertEqual([row["timestamp_ms"] for row in rows], [11, 22])
        self.assertTrue(all(row["stored_crc_valid"] for row in rows))
        self.assertEqual([row["pack_voltage_deci_v"] for row in rows], [4011, 4022])

    def test_csv_export_preserves_samples_across_partial_flushes(self):
        """The separate production CSV dump path must also skip no records after a partial flush.

        Parse the independent production CSV path for its partial scenario. Require
        11/22-ms rows and record_crc_valid=1, covering a separate export implementation.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        rows = list(csv.DictReader(io.StringIO(self.capture("csv").decode("ascii"))))
        self.assertEqual([int(row["timestamp_ms"]) for row in rows], [11, 22])
        self.assertTrue(all(row["record_crc_valid"] == "1" for row in rows))

    def test_production_stream_crc_is_accepted_by_serial_cli(self):
        """The actual script CLI must accept the production C stream CRC using fake UART bytes.

        Serve the production partial capture through BytesIO FakeSerial, mock sleeps
        and argv, run the actual exporter main, then require the saved .bin exactly matches
        the C stream. The fake checks ASCII b and never opens a physical UART.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = self.capture("partial")

        class FakeSerial(io.BytesIO):
            """Serve a production capture through the minimal serial interface used by the CLI."""

            def __init__(self, *args, **kwargs):
                """Start reads at the first captured firmware byte regardless of UART options.

                Accept arbitrary serial-constructor args/kwargs solely for interface compatibility.
                Initialize BytesIO with the enclosing production capture and return None; UART
                options do not select any device or alter the deterministic byte source."""
                super().__init__(capture)

            def reset_input_buffer(self):
                """Leave the deterministic capture intact when the CLI requests buffer reset.

                Return None without changing BytesIO position/content so the captured firmware
                header remains available. This emulates only the reset method required by exporter
                main and intentionally has no device buffer side effect."""

            def reset_output_buffer(self):
                """Ignore output reset because commands are asserted rather than queued.

                Return None; no outbound queue exists in this fixture. The write method checks
                the only supported request directly, so buffer clearing must not change input bytes."""

            def write(self, command):
                """Check the binary export request without replacing the capture contents.

                Input command must be exactly ASCII b. Return its byte count without replacing
                the BytesIO capture or advancing reads; raise AssertionError for any other command.
                This proves the CLI export request while performing no serial transmission."""
                if command != b"b":
                    raise AssertionError("Unexpected UART export command")
                return len(command)

        prefix = Path(self.temporary.name) / "export"
        with patch.object(self.export, "serial", types.SimpleNamespace(Serial=FakeSerial)), \
                patch.object(self.export.time, "sleep"), \
                patch.object(self.export.sys, "argv", ["dump_emmc_bin.py", "--port", "fake",
                                                      "--output-prefix", str(prefix)]), \
                patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(self.export.main(), 0)
        self.assertEqual(prefix.with_suffix(".bin").read_bytes(), capture)

    def test_dump_then_append_crosses_block_boundary_once(self):
        """Repeated flush/dump followed by slots 12 and 13 must retain all thirteen records.

        Decode the C slots scenario after repeated dump/flush and append through slot
        13. Require ordered timestamps 1..13 and every stored CRC flag valid.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        rows = self.unpack_rows(self.capture("slots"))
        self.assertEqual([row["timestamp_ms"] for row in rows], list(range(1, 14)))
        self.assertTrue(all(row["stored_crc_valid"] for row in rows))

    def test_full_capacity_rejects_next_record_without_counting_it(self):
        """A one-data-block fake card must export its twelve records and overflow flag.

        Run a one-data-block fake card. Require header count 12, overflow flag 1 and
        exact timestamps 1..12; the rejected next sample must not affect exported count.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = self.capture("capacity")
        header = self.export.unpack_header(capture[:24])
        self.assertEqual(header["record_count"], 12)
        self.assertEqual(header["flags"], 1)
        self.assertEqual([row["timestamp_ms"] for row in self.unpack_rows(capture)],
                         list(range(1, 13)))

    def test_unflushed_append_automatically_crosses_block_boundary(self):
        """A thirteen-sample burst without caller flushes must produce two contiguous blocks.

        Run the automatic thirteen-sample burst with no caller flushes. Require
        contiguous ordered timestamps 1..13 and valid stored flags across two blocks.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        rows = self.unpack_rows(self.capture("automatic"))
        self.assertEqual([row["timestamp_ms"] for row in rows], list(range(1, 14)))
        self.assertTrue(all(row["stored_crc_valid"] for row in rows))

    def test_reset_session_discards_old_tail_and_restarts_counters(self):
        """Explicit reset must release a retained tail and export only the new session sample.

        Run the explicit reset case. Require session ID 2 and only timestamp 99,
        showing old retained-tail data is not reused as a new-session record.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = self.capture("reset")
        self.assertEqual(self.export.unpack_header(capture[:24])["session_id"], 2)
        self.assertEqual([row["timestamp_ms"] for row in self.unpack_rows(capture)], [99])

    def test_status_does_not_double_count_retained_tail_allocation(self):
        """Usage counters must omit retained-tail bytes already covered by written_blocks.

        Run the C status scenario, which checks retained-tail usage counters, then
        require all thirteen exported timestamps remain ordered and intact.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        rows = self.unpack_rows(self.capture("status"))
        self.assertEqual([row["timestamp_ms"] for row in rows], list(range(1, 14)))

    def test_write_read_and_uart_failures_are_retryable(self):
        """Injected data/header/read/UART errors must not shift records or lose successful counts.

        Run the adapter errors mode, which injects data/header/read/UART failures.
        Require ordered timestamps 1..12 and valid stored CRCs after its retry sequence.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        rows = self.unpack_rows(self.capture("errors"))
        self.assertEqual([row["timestamp_ms"] for row in rows], list(range(1, 13)))
        self.assertTrue(all(row["stored_crc_valid"] for row in rows))

    def test_empty_session_flush_is_repeatable(self):
        """An empty session must export a header/footer with zero records and zero stream CRC.

        Run the empty-session case. Require zero decoded records and footer stream
        CRC zero; header/footer framing is still validated.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = self.capture("empty")
        self.assertEqual(self.unpack_rows(capture), [])
        self.assertEqual(self.export.unpack_footer(capture[-16:])["stream_crc"], 0)

    def test_storage_corruption_is_reported_in_export_record_flag(self):
        """A damaged fake eMMC byte must survive export only with the stored CRC invalid flag.

        Run the fake-card corrupt case. Require the first stored CRC flag false but
        its newly produced dump CRC true, and the unaffected second stored flag true.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        rows = self.unpack_rows(self.capture("corrupt"))
        self.assertFalse(rows[0]["stored_crc_valid"])
        self.assertTrue(rows[0]["dump_crc_valid"])
        self.assertTrue(rows[1]["stored_crc_valid"])

    def test_reader_round_trips_all_signed_fields_and_raw_bytes(self):
        """The portable capture reader must preserve C units, signed extrema and exact wire bytes.

        Decode the C partial capture while copying raw output. Compare known raw units
        and signed extrema (-32768/32767 deciamps, -128 C) plus flags; copied bytes must match.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = self.capture("partial")
        raw_output = io.BytesIO()
        header, rows = self.export.read_dump(io.BytesIO(capture), raw_output)
        self.assertEqual(header["record_count"], 2)
        self.assertEqual(raw_output.getvalue(), capture)
        expected = {
            "timestamp_ms": 11, "pack_voltage_deci_v": 4011,
            "pack_summed_voltage_deci_v": 3999, "lowest_cell_voltage_100uv": 30000,
            "average_cell_voltage_100uv": 35000, "highest_cell_voltage_100uv": 40000,
            "pack_current_deci_a": -1234, "lowest_pack_current_deci_a": -32768,
            "average_pack_current_deci_a": -12, "highest_pack_current_deci_a": 32767,
            "lowest_pack_power_kw": -99, "average_pack_power_kw": -1,
            "highest_pack_power_kw": 101, "pack_soc": 87, "highest_temp_c": 100,
            "average_temp_c": -10, "lowest_temp_c": -128, "fault_flags": 0xA5,
            "record_flags": 1, "stored_crc_valid": True, "dump_crc_valid": True,
        }
        for field, value in expected.items():
            with self.subTest(field=field):
                self.assertEqual(rows[0][field], value)

    def test_wire_corruption_is_rejected_at_header_stream_and_footer(self):
        """Single-byte damage at each framing layer must be rejected by the actual capture reader.

        Flip one byte in the header, first record and final footer CRC independently.
        Require the reader to identify Header CRC, Stream CRC and Footer CRC failures.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = self.capture("partial")
        for offset, message in [(8, "Header CRC"), (24, "Stream CRC"),
                                (len(capture) - 1, "Footer CRC")]:
            with self.subTest(offset=offset):
                damaged = bytearray(capture)
                damaged[offset] ^= 1
                with self.assertRaisesRegex(RuntimeError, message):
                    self.export.read_dump(io.BytesIO(damaged))

    def test_footer_count_mismatch_is_rejected_with_valid_crc(self):
        """A self-consistent footer CRC cannot excuse a count different from the header.

        Change footer record count to three and recompute its own CRC. Require the
        header/footer count comparison to fail despite internally consistent footer bytes.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = bytearray(self.capture("partial"))
        struct.pack_into("<I", capture, len(capture) - 12, 3)
        struct.pack_into("<I", capture, len(capture) - 4,
                         self.export.crc32_bytes(capture[-16:-4]))
        with self.assertRaisesRegex(RuntimeError, "Footer record count"):
            self.export.read_dump(io.BytesIO(capture))

    def test_unsupported_version_and_truncated_capture_are_rejected(self):
        """Future versions and missing record/footer bytes must not be treated as valid captures.

        Set version 2 and recompute header CRC, then truncate a valid capture at several
        header/record/footer boundaries. Require version or exact-read errors respectively.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = self.capture("partial")
        versioned = bytearray(capture)
        struct.pack_into("<H", versioned, 4, 2)
        struct.pack_into("<I", versioned, 20, self.export.crc32_bytes(versioned[:20]))
        with self.assertRaisesRegex(RuntimeError, "Unsupported dump version"):
            self.export.read_dump(io.BytesIO(versioned))
        for size in [0, 23, 24, 65, len(capture) - 1]:
            with self.subTest(size=size):
                with self.assertRaisesRegex(RuntimeError, "reading"):
                    self.export.read_dump(io.BytesIO(capture[:size]))

    def test_offline_cli_works_without_serial_and_keeps_input(self):
        """A fake-eMMC capture must export to CSV without a UART library or device.

        Write a production partial capture, patch serial unavailable and run --input-bin.
        Require successful .bin/.csv export, unchanged source and raw decivolt field 4011.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = self.capture("partial")
        source = Path(self.temporary.name) / "source.bin"
        prefix = Path(self.temporary.name) / "offline"
        source.write_bytes(capture)
        with patch.object(self.export, "serial", None), \
                patch.object(self.export.sys, "argv", ["dump_emmc_bin.py", "--input-bin", str(source),
                                                      "--output-prefix", str(prefix)]), \
                patch("sys.stdout", new=io.StringIO()):
            self.assertEqual(self.export.main(), 0)
        self.assertEqual(source.read_bytes(), capture)
        self.assertEqual(prefix.with_suffix(".bin").read_bytes(), capture)
        self.assertIn("4011", prefix.with_suffix(".csv").read_text())

    def test_offline_cli_rejects_self_overwrite_and_trailing_data(self):
        """Reject input/output path aliases and malformed trailing bytes before claiming success.

        Provide an empty stream with trailing bytes. First require output-alias rejection
        and preserved source, then use a distinct prefix and require trailing-data rejection.
        Return None on success; unittest assertions or unexpected errors fail this case."""
        capture = self.capture("empty")
        source = Path(self.temporary.name) / "trailing.bin"
        source.write_bytes(capture + b"unexpected")
        with patch.object(self.export.sys, "argv", ["dump_emmc_bin.py", "--input-bin", str(source),
                                                  "--output-prefix", str(source.with_suffix(""))]):
            with self.assertRaisesRegex(RuntimeError, "Input binary path"):
                self.export.main()
        self.assertEqual(source.read_bytes(), capture + b"unexpected")
        with patch.object(self.export.sys, "argv", ["dump_emmc_bin.py", "--input-bin", str(source),
                                                  "--output-prefix", str(source.parent / "out")]):
            with self.assertRaisesRegex(RuntimeError, "trailing bytes"):
                self.export.main()


if __name__ == "__main__":
    unittest.main()
