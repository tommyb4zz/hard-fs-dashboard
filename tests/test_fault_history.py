"""Compile and exercise the production append-only fault ledger under ASan/UBSan."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class FaultHistoryTests(unittest.TestCase):
    """Run each independent ledger scenario in a fresh process without a clearing API."""

    @classmethod
    def setUpClass(cls):
        """Compile the production ledger and its driver once with sanitizers."""
        cls.root = Path(__file__).resolve().parents[1]
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            raise RuntimeError("A host C compiler is required for fault-history tests")
        temporary = tempfile.TemporaryDirectory(prefix="dashboard-fault-history-")
        cls.addClassCleanup(temporary.cleanup)
        cls.executable = Path(temporary.name) / "fault_history_tests"
        compiled = subprocess.run([
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-I" + str(cls.root / "Inc"),
            str(cls.root / "tests/fault_history/test_fault_history.c"),
            str(cls.root / "Src/app_fault_history.c"), "-o", str(cls.executable),
        ], capture_output=True, text=True, timeout=30)
        if compiled.returncode != 0:
            raise RuntimeError("Fault-history compilation failed:\n" + compiled.stderr)

    def run_case(self, name, *arguments):
        """Run one ledger scenario in its own process; the ledger has no reset API."""
        result = subprocess.run([str(self.executable), name, *arguments], capture_output=True,
                                text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_transitions_and_recurrence_keep_immutable_history(self):
        """Recovery and recurrence append; earlier events never change."""
        self.run_case("lifecycle")

    def test_invalid_inputs_and_independent_sources(self):
        """Invalid sources/states are ignored and each source keeps its own state."""
        self.run_case("guards")

    def test_full_history_never_overwrites_and_reports_loss(self):
        """A full RAM ledger keeps every event and counts the ones it could not keep."""
        self.run_case("overflow")

    def test_journal_recovery_continues_sequence_in_a_new_session(self):
        """A reboot continues numbering after saved events with a new boot ID."""
        self.run_case("recover")

    def test_failed_writes_retain_pending_records_and_retry_in_order(self):
        """Failed writes keep events in RAM and retry them in order."""
        self.run_case("retry")

    def test_uninterpretable_final_record_blocks_writes_without_losing_pending_records(self):
        """A checksum-valid record this firmware cannot read blocks writes but keeps new events in RAM."""
        for kind in ("sequence", "source", "state"):
            with self.subTest(kind=kind):
                self.run_case("corrupt-" + kind)

    def test_torn_final_write_is_skipped_never_rewritten(self):
        """A torn final slot (including the first) is skipped and never rewritten."""
        for case in ("torn-tail", "torn-first"):
            with self.subTest(case=case):
                self.run_case(case)

    def test_foreign_card_data_blocks_writes(self):
        """Several unreadable final slots are treated as foreign data and block writes."""
        self.run_case("foreign")

    def test_read_error_while_attaching_keeps_events_in_ram_and_allows_retry(self):
        """An I/O error while attaching leaves the journal detached and retryable."""
        self.run_case("attach-io-error")

    def test_journal_state_explains_saving(self):
        """The journal state reports saved, retrying, full or no storage."""
        self.run_case("journal-states")

    def test_end_of_large_journal_found_with_logarithmic_reads(self):
        """The end of a 2^20-slot journal is found in about 21 reads for many journal lengths."""
        for end in (0, 1, 2, 3, 1000, 524288, (1 << 20) - 1, 1 << 20):
            with self.subTest(end=end):
                self.run_case("search", str(end))

    def test_full_journal_never_reuses_an_occupied_slot(self):
        """A full journal never writes over a slot; new events stay listed from RAM."""
        self.run_case("disk-full")

    def test_invalid_attachment_is_retryable_and_live_backend_cannot_be_replaced(self):
        """Bad attach arguments can be retried; a live journal cannot be swapped."""
        self.run_case("attach-guards")

    def test_exhausted_session_namespace_blocks_new_durable_writes(self):
        """A journal at the last boot ID blocks writes instead of wrapping to zero."""
        self.run_case("session-full")


if __name__ == "__main__":
    unittest.main()
