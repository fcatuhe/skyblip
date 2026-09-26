#!/usr/bin/env python3
"""Self-check for power_budget.py: the deltas, the postures, and the caveats.

    python3 scripts/test_power_budget.py

Stdlib alone, no radio and no clock. Every record here is handcrafted bytes put
through the same chunk decoder and session sink blip.py fetch uses, so a Duty
field that moves in firmware/core/diag/payload.h fails here rather than reading
as a quiet zero.
"""
import base64
import contextlib
import io
import json
import pathlib
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip_records as records  # noqa: E402
import power_budget  # noqa: E402

PAYLOAD_H = pathlib.Path(__file__).resolve().parents[1] / "firmware" / "core" / "diag" / "payload.h"

BOOT = 1
POWER = 8
GAP = 16
END = 17
DUTY = 18

PHASE_VALID = 0x01
UTC_DATED = 0x02
CHARGING = 0x04
EXTERNAL_POWER = 0x08
GAUGE_VALID = 0x10

FIX_DATED_AT = 1_700_000_000

ERASED = b"\xff" * 24

LEVELS = {name: code for code, name in enumerate(records.POWER_LEVEL)}


def slot(type_id, payload, flags, at_s, into_ms=0):
    raw = bytearray(24)
    raw[0] = type_id
    raw[1] = flags
    raw[2:4] = into_ms.to_bytes(2, "little")
    raw[4:8] = at_s.to_bytes(4, "little")
    raw[8:8 + len(payload)] = payload
    return bytes(raw)


def duty(at_s, partial=0, full=0, backlight_ms=0, rx_armed_ms=0, tx_keyed_ms=0,
         ble_connected_ms=0, annunciator_ms=0, flags=PHASE_VALID):
    counters = (partial, full, backlight_ms, rx_armed_ms, tx_keyed_ms, ble_connected_ms,
                annunciator_ms)
    payload = b"".join((value % power_budget.WRAP).to_bytes(2, "little") for value in counters)
    return slot(DUTY, payload, flags, at_s)


def power(at_s, cell_mv=4187, percent=100, level="normal", flags=PHASE_VALID | GAUGE_VALID):
    payload = bytearray(16)
    payload[0:2] = cell_mv.to_bytes(2, "little")
    payload[10] = percent
    payload[11] = LEVELS[level]
    return slot(POWER, bytes(payload), flags, at_s)


def gap(at_s, dropped=12, span_ms=360000):
    payload = bytearray(16)
    payload[0:4] = dropped.to_bytes(4, "little")
    payload[4:8] = span_ms.to_bytes(4, "little")
    return slot(GAP, bytes(payload), PHASE_VALID, at_s)


def boot(at_s):
    return slot(BOOT, bytes(16), PHASE_VALID, at_s)


def end(at_s, flags=PHASE_VALID):
    return slot(END, bytes(16), flags, at_s)


def discharging(seconds, from_percent=100, to_percent=0, closing_level="cutoff", rx_share=0.0,
                start_s=0, clock=0, period_s=30):
    """A Power and a Duty record every period, then the pair a parking capture closes on."""
    raws = []
    for at in list(range(0, seconds, period_s)) + [seconds]:
        percent = round(from_percent + (to_percent - from_percent) * at / seconds)
        level = closing_level if at == seconds else "normal" if percent > 20 else "low"
        raws.append(power(start_s + at, cell_mv=4187 - 986 * at // seconds, percent=percent,
                          level=level, flags=PHASE_VALID | GAUGE_VALID | clock))
        raws.append(duty(start_s + at, rx_armed_ms=int(rx_share * at * 1000),
                         flags=PHASE_VALID | clock))
    return raws


def whole_run(seconds, **kwargs):
    return [boot(0)] + discharging(seconds, **kwargs) + [end(seconds)]


def session_lines(raws, session=1, closed=True, truncated=False, missing=()):
    """What blip.py fetch writes for one session: the chunk decoder, then the session sink."""
    encoded = base64.b64encode(b"".join(raws)).decode("ascii")
    decoded = records.decode_chunk("diagnostics", encoded, 0, 0)
    lines = []
    sink = records.SessionSink("diagnostics", {"session": session, "records": len(raws),
                                               "closed": closed, "truncated": truncated},
                               lines.append)
    for record in decoded:
        if record["index"] not in missing:
            sink.write(record)
    sink.finish(complete=True)
    return lines


class Case(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.folder = pathlib.Path(folder.name)

    def capture(self, lines):
        path = self.folder / "capture.ndjson"
        path.write_text("".join(json.dumps(line) + "\n" for line in lines), encoding="utf-8")
        return str(path)

    def only_run(self, raws, **kwargs):
        runs = power_budget.read(self.capture(session_lines(raws, **kwargs)))
        self.assertEqual(len(runs), 1)
        return runs[0]

    def text(self, run, pack_mah=None):
        lines = []
        power_budget.report(run, pack_mah, lines.append)
        return "\n".join(lines)


class Deltas(Case):
    def test_a_counter_that_wrapped_gives_the_movement_and_not_a_negative(self):
        before = self.only_run([duty(0, rx_armed_ms=65500)]).duty[0]
        after = self.only_run([duty(30, rx_armed_ms=100)]).duty[0]
        self.assertEqual(power_budget.delta(before, after, "rx_armed_ms"), 136)

    def test_the_dwell_map_reads_as_the_receiver_being_armed_most_of_the_second(self):
        run = self.only_run([duty(0), duty(30, rx_armed_ms=29670)])
        charge, seconds, _, _ = power_budget.model(run)
        self.assertEqual(seconds, 30)
        self.assertAlmostEqual(charge["868 MHz receive"], 4.8 * 29.67, places=3)

    def test_a_panel_refresh_costs_its_waveform_and_not_the_whole_interval(self):
        run = self.only_run([duty(0), duty(30, partial=2, full=1)])
        charge, _, _, _ = power_budget.model(run)
        self.assertAlmostEqual(charge["panel partial refresh"], 8.0 * 2 * 0.460, places=3)
        self.assertAlmostEqual(charge["panel full refresh"], 8.0 * 2.5, places=3)

    def test_the_whole_budget_of_one_flying_interval(self):
        run = self.only_run([duty(0), duty(30, partial=1, rx_armed_ms=29670, tx_keyed_ms=159)])
        charge, seconds, _, _ = power_budget.model(run)
        self.assertAlmostEqual(sum(charge.values()) / seconds, 40.725, places=3)


class Postures(Case):
    def test_keying_just_under_the_threshold_reads_parked(self):
        run = self.only_run([duty(0), duty(60, tx_keyed_ms=159)])
        _, _, held, _ = power_budget.model(run)
        self.assertEqual(held["parked"], 60)
        self.assertEqual(held["airborne"], 0)

    def test_keying_at_the_threshold_reads_airborne(self):
        run = self.only_run([duty(0), duty(60, tx_keyed_ms=160)])
        _, _, held, _ = power_budget.model(run)
        self.assertEqual(held["airborne"], 60)
        self.assertEqual(held["parked"], 0)


class Holes(Case):
    def refusal(self, raws, **kwargs):
        charge, seconds, _, skipped = power_budget.model(self.only_run(raws, **kwargs))
        self.assertEqual(seconds, 0)
        self.assertEqual(sum(charge.values()), 0)
        return skipped

    def test_an_interval_the_ring_refused_records_inside_is_not_budgeted(self):
        self.assertEqual(self.refusal([duty(0), gap(15), duty(30, rx_armed_ms=29670)]),
                         ["the ring refused records inside the interval"])

    def test_a_gap_is_named_before_any_number_is_printed(self):
        report = self.text(self.only_run([duty(0), gap(15, dropped=412), duty(30)]))
        self.assertIn("412 records the ring had to refuse", report)
        self.assertIn("nothing to budget", report)

    def test_the_interval_a_fix_dated_mid_run_cannot_be_subtracted(self):
        dated = duty(FIX_DATED_AT, flags=PHASE_VALID | UTC_DATED)
        self.assertEqual(self.refusal([duty(10), dated]),
                         ["the clock became UTC-dated inside the interval"])

    # A missing Duty record hides a wrap: the pair either side can be 60 s of receiver apart.
    def test_an_interval_with_an_index_missing_inside_is_not_budgeted(self):
        run = self.only_run([duty(0), power(15), duty(30, rx_armed_ms=100)], missing=(1,))
        _, seconds, _, skipped = power_budget.model(run)
        self.assertEqual(seconds, 0)
        self.assertEqual(skipped, ["indices are missing inside the interval"])
        self.assertIn("missing indices 1..1", self.text(run))

    def test_an_interval_with_an_unreadable_slot_inside_is_not_budgeted(self):
        run = self.only_run([duty(0), ERASED, duty(30, rx_armed_ms=100)])
        _, seconds, _, skipped = power_budget.model(run)
        self.assertEqual(seconds, 0)
        self.assertEqual(skipped, ["a slot inside the interval is unreadable"])
        self.assertIn("unreadable at index 1: erased", self.text(run))

    def test_an_interval_the_device_rebooted_inside_is_not_budgeted(self):
        self.assertEqual(self.refusal([duty(0), boot(10), duty(30)]),
                         ["the device booted inside the interval"])

    def test_two_duty_records_further_apart_than_a_counter_can_span_are_not_subtracted(self):
        self.assertEqual(self.refusal([duty(0), duty(61, rx_armed_ms=100)]),
                         ["the interval is longer than the 60 s a counter can span"])

    def test_an_interval_on_usb_is_not_budgeted_as_drain(self):
        self.assertEqual(
            self.refusal([duty(0), power(30, flags=PHASE_VALID | GAUGE_VALID | EXTERNAL_POWER),
                          duty(30)]),
            ["the cell was on external power or charging"])

    def test_two_sessions_are_never_subtracted_across(self):
        path = self.capture(session_lines([duty(0)], session=1)
                            + session_lines([duty(30, rx_armed_ms=100)], session=2))
        for run in power_budget.read(path):
            self.assertEqual(power_budget.model(run)[1], 0)

    def test_a_session_the_device_did_not_close_says_so_and_names_the_dropped_tail(self):
        report = self.text(self.only_run([duty(0), duty(30), duty(60)], closed=False))
        self.assertIn("session 1: not closed", report)
        self.assertIn("session 1: index 2 dropped", report)

    def test_a_run_whose_first_sector_was_recycled_says_so(self):
        report = self.text(self.only_run([duty(0), duty(30)], truncated=True))
        self.assertIn("session 1: truncated", report)


class Measured(Case):
    def measure(self, raws, pack_mah=2400):
        return power_budget.measure(self.only_run(raws), pack_mah)

    def test_a_run_from_full_to_cutoff_spends_the_pack_and_needs_no_curve(self):
        cell, _ = self.measure(whole_run(7200))
        self.assertTrue(cell["whole"])
        self.assertAlmostEqual(cell["mah"], 2400)
        self.assertAlmostEqual(cell["milliamps"], 1200)

    def test_a_run_a_fix_dated_after_it_started_is_timed_on_the_clock_it_ended_on(self):
        head = [boot(0), power(0), duty(0), power(30), duty(30)]
        tail = discharging(7200, start_s=FIX_DATED_AT, clock=UTC_DATED)
        cell, _ = self.measure(head + tail + [end(FIX_DATED_AT + 7200, flags=UTC_DATED)])
        self.assertTrue(cell["whole"])
        self.assertAlmostEqual(cell["hours"], 2)
        self.assertAlmostEqual(cell["milliamps"], 1200)

    def test_a_cutoff_whose_reading_failed_the_sanity_floor_still_ends_a_whole_run(self):
        raws = whole_run(7200)
        raws[-3] = power(7200, cell_mv=2100, percent=0, level="cutoff", flags=PHASE_VALID)
        cell, _ = self.measure(raws)
        self.assertTrue(cell["whole"])
        self.assertAlmostEqual(cell["milliamps"], 1200)

    def test_a_run_that_did_not_end_at_cutoff_says_it_is_not_a_whole_run(self):
        run = self.only_run(whole_run(7200, to_percent=40, closing_level="normal"))
        cell, _ = power_budget.measure(run, 2400)
        self.assertFalse(cell["whole"])
        self.assertIn("not a whole run: it did not end at cutoff, the last level read normal",
                      self.text(run, 2400))

    def test_a_run_that_did_not_start_full_says_it_is_not_a_whole_run(self):
        run = self.only_run(whole_run(7200, from_percent=70))
        self.assertIn("not a whole run: it did not start full", self.text(run, 2400))

    def test_a_partial_run_is_read_off_the_gauge_and_says_that_it_was(self):
        run = self.only_run(whole_run(3600, from_percent=80, to_percent=60, closing_level="normal"))
        cell, _ = power_budget.measure(run, 2400)
        self.assertFalse(cell["whole"])
        self.assertAlmostEqual(cell["mah"], 480)
        self.assertIn("points of the gauge's own curve", self.text(run, 2400))

    def test_a_gauge_that_did_not_fall_is_no_measurement_and_says_so(self):
        run = self.only_run(whole_run(3600, from_percent=80, to_percent=80, closing_level="normal"))
        cell, why = power_budget.measure(run, 2400)
        self.assertIsNone(cell)
        self.assertEqual(why, "the gauge did not fall, 80% to 80%")
        self.assertIn("no measured draw: the gauge did not fall", self.text(run, 2400))

    def test_a_gauge_that_rose_is_no_measurement_and_never_a_negative_draw(self):
        cell, why = self.measure(whole_run(3600, from_percent=60, to_percent=65,
                                           closing_level="normal"))
        self.assertIsNone(cell)
        self.assertEqual(why, "the gauge did not fall, 60% to 65%")

    def test_a_run_on_the_charger_throughout_is_no_measurement(self):
        raws = [power(at, percent=40 + at // 90, flags=PHASE_VALID | GAUGE_VALID | CHARGING)
                for at in range(0, 3600, 30)]
        cell, why = self.measure(raws)
        self.assertIsNone(cell)
        self.assertEqual(why, "fewer than two believable readings on battery alone")

    def test_a_run_unplugged_after_an_hour_is_measured_from_the_unplug(self):
        plugged = [power(at, percent=100, flags=PHASE_VALID | GAUGE_VALID | EXTERNAL_POWER)
                   for at in range(0, 3600, 30)]
        cell, _ = self.measure([boot(0)] + plugged + discharging(7200, start_s=3600))
        self.assertTrue(cell["whole"])
        self.assertAlmostEqual(cell["hours"], 2)

    def test_a_reading_the_sanity_floor_threw_away_is_not_a_measurement(self):
        run = self.only_run([power(0, flags=PHASE_VALID), duty(0),
                             power(30, flags=PHASE_VALID), duty(30)])
        self.assertIsNone(power_budget.measure(run, 2400)[0])
        self.assertIn("no measured draw", self.text(run, 2400))

    def test_the_residual_is_what_the_table_does_not_explain(self):
        run = self.only_run(whole_run(3600, rx_share=0.5))
        charge, seconds, _, _ = power_budget.model(run)
        # GNSS 29 + MCU 3.5 + IMU 0.6 + baro 0.3 + half of receive 4.8 and TCXO 2.0 = 36.8 mA
        self.assertAlmostEqual(sum(charge.values()) / seconds, 36.8, places=6)
        self.assertAlmostEqual(power_budget.measure(run, 40.0)[0]["milliamps"], 40.0)
        report = self.text(run, 40.0)
        self.assertRegex(report, r"residual +-3\.20 +the table explains 92% of what the cell lost")


class Caveats(Case):
    def test_every_uncited_current_is_counted_out_loud(self):
        cited = sum(1 for consumer in power_budget.CONSUMERS if consumer.cited)
        self.assertEqual(cited, 3)
        report = self.text(self.only_run([duty(0), duty(30)]))
        self.assertIn("no current in the table was metered on this board: 3 of 12 cite a "
                      "datasheet, 9 are estimates", report)

    def test_a_row_citing_a_datasheet_names_the_table_it_came_from(self):
        for consumer in power_budget.CONSUMERS:
            self.assertGreater(consumer.milliamps, 0, consumer.name)
            self.assertTrue(consumer.source, consumer.name)
            if consumer.cited:
                self.assertIn("table", consumer.source.lower(), consumer.name)

    def test_a_file_with_no_diagnostics_session_is_an_error_and_not_an_empty_budget(self):
        empty = self.capture([])
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(power_budget.main([empty]), 1)

    def test_a_pack_of_no_capacity_is_refused_at_the_command_line(self):
        path = self.capture(session_lines(whole_run(3600)))
        for mah in ("0", "-2400", "nan"):
            with self.assertRaises(SystemExit) as refused, \
                    contextlib.redirect_stderr(io.StringIO()):
                power_budget.main([path, "--pack-mah", mah])
            self.assertEqual(refused.exception.code, 2, mah)


class Fields(Case):
    def test_every_duty_field_the_budget_reads_is_a_field_the_decoder_decodes(self):
        decoded = records.decode_diag_record(duty(0))
        for consumer in power_budget.CONSUMERS:
            kind, field, _ = consumer.counter
            if kind != "elapsed":
                self.assertIn(field, decoded)

    def test_the_longest_interval_the_budget_subtracts_is_the_firmwares_bound(self):
        bound = re.search(r"kDutyMaxPeriodMs = (\d+);", PAYLOAD_H.read_text(encoding="utf-8"))
        self.assertIsNotNone(bound, "kDutyMaxPeriodMs moved out of %s" % PAYLOAD_H)
        self.assertEqual(power_budget.DUTY_MAX_PERIOD_S * 1000, int(bound.group(1)))


if __name__ == "__main__":
    unittest.main(verbosity=2)
