"""reception_check.py names a Bravo's outages and what its receiver and reception records blame."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import reception_check as check  # noqa: E402

START_S = 1_800_000_000


class Sky:
    """A Full capture's receiver and reception records, a second at a time."""

    def __init__(self):
        self.records = [{"index": 0, "type": "boot", "at_s": START_S, "utc_dated": True}]
        self.at_s = START_S
        self.totals = dict.fromkeys(check.WIDE_COUNTERS + check.NARROW_COUNTERS, 0)
        self.receiver = {"listening": True, "irq_armed": True, "probed": True,
                         "noise_dbm": -118, "threshold_dbm": -110, "part_mode": "receive",
                         "part_reset": "none", "front_end_kept": True}
        self.reception = {"aircraft": 3, "tracked": 3, "reference": "own", "fix_valid": True}

    def seconds(self, count, step_s=1, **per_second):
        for _ in range(count):
            self.at_s += step_s
            for field, step in per_second.items():
                self.totals[field] += step
            self.write()

    def mark(self, **record):
        self.records.append(dict(record, index=len(self.records), at_s=self.at_s))

    def write(self):
        wide = {field: self.totals[field] % check.WIDE for field in check.WIDE_COUNTERS}
        narrow = {field: self.totals[field] % check.NARROW for field in check.NARROW_COUNTERS}
        envelope = {"at_s": self.at_s, "utc_dated": True}
        receiver = dict(envelope, type="receiver", frames=wide["frames"],
                        part_packets=wide["part_packets"], **narrow, **self.receiver)
        reception = dict(envelope, type="reception",
                         **{field: wide[field] for field in check.WIDE_COUNTERS[2:]},
                         **self.reception)
        for record in (receiver, reception):
            record["index"] = len(self.records)
            self.records.append(record)

    def report(self, min_seconds=10):
        return "\n".join(check.report(1, check.seconds_of(self.records), min_seconds))


HEARING = {"frames": 60, "part_packets": 60, "crc": 5, "folded": 50, "positions": 20}


class Outages(unittest.TestCase):

    def test_a_healthy_run_reports_no_outage(self):
        sky = Sky()
        sky.seconds(60, **HEARING)
        self.assertIn("0 reception outages of 10 s or more", sky.report())

    def test_a_brown_out_is_blamed_on_the_part_that_lost_its_configuration(self):
        sky = Sky()
        sky.seconds(20, **HEARING)
        sky.receiver.update(part_mode="standby_rc", part_reset="power_on", threshold_dbm=-74,
                            front_end_kept=False)
        sky.seconds(1, part_resets=1)
        sky.receiver.update(part_reset="none")
        sky.seconds(29)
        report = sky.report()
        self.assertIn("1 reception outages", report)
        self.assertIn("30 s: no position placed", report)
        self.assertIn("the LR2021 reset itself (power_on)", report)
        self.assertIn("left receive behind the driver's back: standby_rc", report)
        self.assertIn("threshold -74 dBm, 0xF30150 lost", report)

    def test_packets_the_part_counted_and_the_driver_never_read_point_at_the_irq(self):
        sky = Sky()
        sky.seconds(20, **HEARING)
        sky.seconds(15, part_packets=60)
        report = sky.report()
        self.assertIn("the part decoded 900 packets the driver never read", report)

    def test_silence_with_the_noise_up_is_jamming_and_without_it_an_empty_sky(self):
        jammed = Sky()
        jammed.seconds(20, **HEARING)
        jammed.receiver["noise_dbm"] = -80
        jammed.seconds(15)
        self.assertIn("noise +38 dB over the run's median", jammed.report())

        quiet = Sky()
        quiet.seconds(20, **HEARING)
        quiet.seconds(15)
        self.assertIn("the antenna, the front end or an empty sky", quiet.report())

    def test_frames_all_refused_for_parity_are_named(self):
        sky = Sky()
        sky.seconds(20, **HEARING)
        sky.seconds(15, frames=40, part_packets=40, crc=40)
        self.assertIn("600 frames heard, every one refused for parity", sky.report())

    def test_traffic_with_no_fix_is_an_outage_on_the_efb_and_not_in_the_receiver(self):
        sky = Sky()
        sky.seconds(20, **HEARING)
        sky.reception["fix_valid"] = False
        sky.seconds(12, **HEARING)
        report = sky.report()
        self.assertIn("0 reception outages", report)
        self.assertIn("traffic placed, no own fix, so nothing reached an EFB", report)

    def test_silences_either_side_of_a_gap_are_not_joined_into_one_outage(self):
        sky = Sky()
        sky.seconds(20, **HEARING)
        sky.seconds(6)
        sky.mark(type="gap")
        sky.seconds(1)
        sky.seconds(6)
        self.assertIn("0 reception outages", sky.report())

    def test_an_outage_is_timed_by_the_seconds_it_spans_not_the_records_it_holds(self):
        sky = Sky()
        sky.seconds(20, **HEARING)
        sky.seconds(2, step_s=check.MAX_STEP_S)
        sky.seconds(1, **HEARING)
        report = sky.report()
        self.assertIn("1 reception outages", report)
        self.assertIn("10 s: no position placed", report)


    def test_a_capture_dated_from_boot_then_by_utc_is_timed_by_what_it_measured(self):
        sky = Sky()
        sky.records[0]["utc_dated"] = False
        sky.at_s = 548
        sky.seconds(3, **HEARING)
        for record in sky.records:
            record["utc_dated"] = False
        sky.at_s = START_S
        sky.seconds(4, **HEARING)
        report = sky.report()
        self.assertIn("session 1: 5 s measured", report)
        self.assertIn("  +550s to 2027-01-15T08:00:04\n", report)
        self.assertNotIn(str(START_S - 550), report)


class Subtraction(unittest.TestCase):

    def test_a_counter_crossing_its_wrap_still_counts_what_happened(self):
        sky = Sky()
        sky.totals["frames"] = check.WIDE - 30
        sky.totals["rearms"] = check.NARROW - 1
        sky.seconds(1)
        sky.seconds(1, frames=60, rearms=2)
        seconds = check.seconds_of(sky.records)
        self.assertEqual(seconds[-1].counts["frames"], 60)
        self.assertEqual(seconds[-1].counts["rearms"], 2)

    def test_the_parts_count_restarting_at_its_reset_is_not_read_as_a_wrap(self):
        sky = Sky()
        sky.seconds(3, **HEARING)
        sky.totals["part_packets"] = 0
        sky.seconds(1, part_resets=1)
        sky.seconds(1)
        seconds = check.seconds_of(sky.records)
        self.assertNotIn("part_packets", seconds[-2].counts)
        self.assertEqual(seconds[-1].counts["part_packets"], 0)

    def test_nothing_is_subtracted_across_an_unreadable_or_a_missing_record(self):
        sky = Sky()
        sky.seconds(3, **HEARING)
        sky.mark(error="unknown type 99")
        sky.seconds(3, **HEARING)
        sky.seconds(4, **HEARING)
        del sky.records[-6:-4]
        self.assertEqual(len(check.seconds_of(sky.records)), 2 + 2 + 2)

    def test_nothing_is_subtracted_across_a_gap_or_a_hole_in_time(self):
        sky = Sky()
        sky.seconds(3, **HEARING)
        sky.records.append({"index": len(sky.records), "type": "gap", "at_s": sky.at_s})
        sky.seconds(3, **HEARING)
        sky.at_s += check.MAX_STEP_S + 1
        sky.seconds(3, **HEARING)
        self.assertEqual(len(check.seconds_of(sky.records)), 2 + 2 + 2)


if __name__ == "__main__":
    unittest.main()
