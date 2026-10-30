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

    def run_case(self, name):
        result = subprocess.run([str(self.executable), name], capture_output=True,
                                text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_transitions_and_recurrence_keep_immutable_history(self):
        self.run_case("lifecycle")

    def test_invalid_inputs_and_independent_sources(self):
        self.run_case("guards")

    def test_full_history_never_overwrites_and_reports_loss(self):
        self.run_case("overflow")

    def test_journal_recovery_continues_sequence_in_a_new_session(self):
        self.run_case("recover")

    def test_failed_writes_retain_pending_records_and_retry_in_order(self):
        self.run_case("retry")

    def test_corruption_blocks_writes_without_losing_pending_records(self):
        for kind in ("read", "sequence", "source", "state"):
            with self.subTest(kind=kind):
                self.run_case("corrupt-" + kind)

    def test_full_journal_never_reuses_an_occupied_slot(self):
        self.run_case("disk-full")

    def test_invalid_attachment_is_retryable_and_live_backend_cannot_be_replaced(self):
        self.run_case("attach-guards")

    def test_exhausted_session_namespace_blocks_new_durable_writes(self):
        self.run_case("session-full")


if __name__ == "__main__":
    unittest.main()
