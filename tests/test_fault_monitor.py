"""Compile and exercise the production malfunction-episode monitor under ASan/UBSan."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class FaultMonitorTests(unittest.TestCase):
    """Each timeline runs in a fresh process because the monitor and ledger are singletons."""

    @classmethod
    def setUpClass(cls):
        """Build the monitor, ledger and timeline driver once with sanitizers enabled."""
        cls.root = Path(__file__).resolve().parents[1]
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            raise RuntimeError("A host C compiler is required for fault-monitor tests")
        temporary = tempfile.TemporaryDirectory(prefix="dashboard-fault-monitor-")
        cls.addClassCleanup(temporary.cleanup)
        cls.executable = Path(temporary.name) / "fault_monitor_tests"
        compiled = subprocess.run([
            compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-I" + str(cls.root / "Inc"),
            str(cls.root / "tests/fault_monitor/test_fault_monitor.c"),
            str(cls.root / "Src/app_fault_monitor.c"),
            str(cls.root / "Src/app_fault_history.c"), "-o", str(cls.executable),
        ], capture_output=True, text=True, timeout=60)
        if compiled.returncode != 0:
            raise RuntimeError("Fault-monitor compilation failed:\n" + compiled.stderr)

    def run_case(self, name):
        """Run one timeline and require a zero exit with no sanitizer report."""
        result = subprocess.run([str(self.executable), name], capture_output=True,
                                text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_silent_bms_is_one_entry_for_fifteen_minutes(self):
        """A BMS that never answers is one entry, not one per timeout."""
        self.run_case("silent-bms")

    def test_bms_that_starts_during_boot_grace_records_nothing(self):
        """A BMS that answers within the boot grace records nothing."""
        self.run_case("late-bms")

    def test_outage_is_one_lost_and_one_clear_entry(self):
        """A 30 s outage is one LOST and one CLEAR entry, nothing per signal."""
        self.run_case("bms-outage")

    def test_unanswered_pid_records_signal_and_timeout_episode_once(self):
        """One unanswered PID records the signal and the timeout episode once each."""
        self.run_case("pid-missing")

    def test_outage_timeouts_are_not_counted_in_the_timeout_episode(self):
        """Timeouts after the link is declared down belong to the BMS link entry, not the count."""
        self.run_case("timeouts-outage")

    def test_signal_stopping_while_siblings_update_is_reported_once(self):
        """A signal that stops while others update is reported once."""
        self.run_case("signal-stops")

    def test_flapping_status_is_one_episode(self):
        """A status toggling every second is one FAULT and one CLEAR."""
        self.run_case("flapping-status")

    def test_unwired_inputs_never_recorded_wired_silent_inputs_once(self):
        """Unwired inputs are never recorded; wired silent inputs once each."""
        self.run_case("inputs")

    def test_rejected_frame_counter_is_summarized_per_episode(self):
        """Rejected frames are one episode with a count, not one entry per frame."""
        self.run_case("rejected")

    def test_storage_errors_outage_and_overflow_are_separate_episodes(self):
        """Write errors, a card outage and overflow are separate episodes."""
        self.run_case("storage")

    def test_configured_limit_records_value_and_default_has_no_limits(self):
        """A configured limit records the value once; no limits exist by default."""
        self.run_case("limits")
        self.run_case("limits-default")

    def test_missing_journal_reported_once_in_ram(self):
        """A missing card is reported once and kept in RAM."""
        self.run_case("no-journal")

    def test_every_event_kind_has_a_readable_description(self):
        """Every event kind has a readable description."""
        self.run_case("descriptions")


if __name__ == "__main__":
    unittest.main()
