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
                         "part_reset": "none", "front_end_kept": True, "gain_step": 13}
        self.reception = {"aircraft": 3, "tracked": 3, "reference": "own", "fix_valid": True}
        self.loop = {"passes": 104, "worst_pass_us": 8000, "worst_service": "adsb"}
        self.framing = None
        self.screening = None
        self.drain = None

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
                        part_packets=wide["part_packets"], rearms=narrow["rearms"], resets=narrow["resets"],
                        part_resets=narrow["part_resets"],
                        command_errors=narrow["command_errors"],
                        missed_edges=narrow["missed_edges"], **self.receiver)
        reception = dict(envelope, type="reception",
                         framing_resyncs=narrow["framing_resyncs"],
                         **{field: wide[field] for field in check.WIDE_COUNTERS[2:8]},
                         **self.reception)
        loop = dict(envelope, type="loop", **self.loop)
        written = [loop, receiver]
        if self.framing is not None:
            framing = {field: self.totals[field] % check.WIDE for field in
                       ("overflow_drops", "resync_drops", "queue_drops", "in_flight",
                        "split_counts", "drains", "drain_busy_ms")}
            framing.update({field: self.totals[field] % check.NARROW for field in
                            ("stranded_resyncs", "short_resyncs")})
            written.append(dict(envelope, type="framing", **framing, **self.framing))
        if self.screening is not None:
            screening = {field: self.totals[field] % check.WIDE for field in
                         ("squitters", "all_calls", "replies", "parity_refused",
                          "one_bit_errors", "unmatched")}
            written.append(dict(envelope, type="screening", **screening, **self.screening))
        if self.drain is not None:
            drain = {field: self.totals[field] % check.WIDE for field in
                     ("irq_wakes", "empty_wakes", "poll_ms", "screen_ms", "busy_wait_ms",
                      "transactions", "repaired", "repairs_refused")}
            written.append(dict(envelope, type="drain", **drain))
        written.append(reception)
        for record in written:
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
        self.assertIn("noise +38 dB over the run's median: jammed", jammed.report())
        self.assertNotIn("as usual", jammed.report())

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

    def test_a_flood_of_noise_that_stalls_the_loop_is_named_while_frames_keep_coming(self):
        sky = Sky()
        sky.seconds(30, **HEARING)
        sky.receiver["noise_dbm"] = -78
        sky.loop = {"passes": 7, "worst_pass_us": 561000, "worst_service": "adsb"}
        sky.seconds(20, frames=1200, part_packets=1200, crc=90, unsupported=900)
        report = sky.report()
        self.assertIn("1 reception outages", report)
        self.assertIn("noise +40 dB over the run's median: jammed or desensed", report)
        self.assertIn("1200 frames a second, 20 times the run's median of 60", report)
        self.assertIn("the service loop stalled: a pass of 561 ms in adsb, as few as 7 passes",
                      report)

    def test_lost_framing_is_named_and_not_taken_for_a_reset(self):
        sky = Sky()
        sky.seconds(20, **HEARING)
        sky.seconds(15, framing_resyncs=1, frames=60, part_packets=60, unsupported=50)
        report = sky.report()
        self.assertIn("the receive FIFO lost its framing and was emptied 15 times", report)
        self.assertNotIn("lost what the driver wrote", report)

    def test_a_threshold_other_than_the_drivers_is_one_the_part_lost(self):
        sky = Sky()
        sky.seconds(20, **HEARING)
        sky.receiver["threshold_dbm"] = -104
        sky.seconds(15, frames=60, part_packets=60, unsupported=50)
        self.assertIn("the LR2021 lost what the driver wrote: threshold -104 dBm", sky.report())

    def test_lost_frames_are_split_by_where_and_a_receiver_thread_hogging_the_cpu_is_named(self):
        sky = Sky()
        sky.framing = {}
        sky.seconds(20, drains=200, drain_busy_ms=40, **HEARING)
        sky.seconds(15, frames=1200, part_packets=1200, unsupported=900, overflow_drops=30,
                    resync_drops=2, queue_drops=50, in_flight=4, drains=200, drain_busy_ms=600)
        report = sky.report()
        self.assertIn("frames lost to FIFO overflows 450, framing resyncs 30, the full queue 750",
                      report)
        self.assertIn("the receiver thread held 60% of the CPU", report)
        self.assertIn("  receiver thread: 6800 drains, 28.7% of the CPU", report)
        self.assertIn("  frames lost: 450 to FIFO overflows, 30 to framing resyncs, 750 to the "
                      "full queue", report)
        self.assertIn("  framing: in_flight 60, split_counts 0, stranded_resyncs 0", report)


    def test_the_screen_says_what_reached_the_decoder_and_a_full_fifo_or_queue_is_named(self):
        sky = Sky()
        sky.framing = {}
        sky.screening = {"worst_drain_us": 400, "fifo_peak_frames": 3, "queue_peak_frames": 5}
        screened = dict(squitters=10, all_calls=2, replies=8, parity_refused=6, one_bit_errors=1,
                        unmatched=7)
        sky.seconds(20, **HEARING, **screened)
        sky.screening = {"worst_drain_us": 2600, "fifo_peak_frames": 17, "queue_peak_frames": 63}
        sky.seconds(15, frames=1200, part_packets=1200, unsupported=1100, **screened)
        report = sky.report()
        # 34 seconds subtracted from 35 records: the first only sets the counters' base.
        self.assertIn("  screened: 714 of 19140 frames to the decoder (340 squitters, 68 all-calls, "
                      "272 replies, 34 one bit off and repaired), 204 refused for parity", report)
        self.assertIn("  replies with no address heard in clear: 238", report)
        self.assertIn("  peaks: a wake held the CPU 2600 us, found 17 of 18 frames in the FIFO, "
                      "left 63 of 64 in the queue", report)
        self.assertIn("  gain step 13", report)
        self.assertIn("a wake found 17 of the FIFO's 18 frames: the drain period is too long",
                      report)
        self.assertIn("the queue to the decoder held 63 of 64 frames: the decoder fell behind",
                      report)


    def test_a_wake_is_split_into_the_spi_it_paid_for_and_repairs_say_what_they_became(self):
        sky = Sky()
        sky.framing = {}
        sky.drain = {}
        sky.seconds(11, drains=60, drain_busy_ms=80, irq_wakes=10, empty_wakes=20, poll_ms=60,
                    screen_ms=5, busy_wait_ms=10, transactions=500, repaired=2,
                    repairs_refused=1, **HEARING)
        report = sky.report()
        self.assertIn("  wakes: 100 on the FIFO IRQ, 200 empty; poll 600 ms (100 waiting on BUSY, "
                      "5000 SPI transactions, 100 us each), screen 50 ms", report)
        self.assertIn("  repairs: 20 kept for an aircraft held, 10 refused", report)


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
