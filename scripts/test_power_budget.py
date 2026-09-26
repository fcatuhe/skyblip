#!/usr/bin/env python3
"""Self-check for power_budget.py: the deltas, the postures, and the caveats.

    python3 scripts/test_power_budget.py

Stdlib alone, no radio and no clock. Every record here is handcrafted bytes put
through the same decoder blip.py uses, so a Duty field that moves in
firmware/core/diag/payload.h fails here rather than reading as a quiet zero.
"""
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip_records as records  # noqa: E402
import power_budget  # noqa: E402

DUTY = 18
POWER = 8
GAP = 16

PHASE_VALID = 0x01
UTC_DATED = 0x02
GAUGE_VALID = 0x10

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


def capture(raws, session=1, closed=True, truncated=False):
    """A fetched NDJSON file: the session note, then one line per record."""
    lines = [{"type": "session", "log": "diagnostics", "session": session,
              "records": len(raws), "closed": closed, "truncated": truncated}]
    for index, raw in enumerate(raws):
        decoded = records.decode_diag_record(raw)
        lines.append(dict(store="diagnostics", session=session, index=index, **decoded))
    handle = tempfile.NamedTemporaryFile("w", suffix=".ndjson", delete=False, encoding="utf-8")
    with handle:
        for line in lines:
            handle.write(json.dumps(line) + "\n")
    return handle.name


def only_run(raws, **kwargs):
    runs = power_budget.read(capture(raws, **kwargs))
    assert len(runs) == 1, runs
    return runs[0]


def text(run, pack_mah=None):
    lines = []
    power_budget.report(run, pack_mah, lines.append)
    return "\n".join(lines)


class Deltas(unittest.TestCase):
    def test_a_counter_that_wrapped_gives_the_movement_and_not_a_negative(self):
        before = only_run([duty(0, rx_armed_ms=65500)]).duty[0]
        after = only_run([duty(30, rx_armed_ms=100)]).duty[0]
        self.assertEqual(power_budget.delta(before, after, "rx_armed_ms"), 136)

    def test_the_dwell_map_reads_as_the_receiver_being_armed_most_of_the_second(self):
        run = only_run([duty(0), duty(30, rx_armed_ms=29670)])
        charge, seconds, _, _ = power_budget.model(run)
        self.assertEqual(seconds, 30)
        self.assertAlmostEqual(charge["868 MHz receive"], 4.8 * 29.67, places=3)

    def test_a_panel_refresh_costs_its_waveform_and_not_the_whole_interval(self):
        run = only_run([duty(0), duty(30, partial=2, full=1)])
        charge, _, _, _ = power_budget.model(run)
        self.assertAlmostEqual(charge["panel partial refresh"], 8.0 * 2 * 0.460, places=3)
        self.assertAlmostEqual(charge["panel full refresh"], 8.0 * 2.5, places=3)

    def test_the_whole_budget_of_one_flying_interval(self):
        run = only_run([duty(0), duty(30, partial=1, rx_armed_ms=29670, tx_keyed_ms=159)])
        charge, seconds, _, _ = power_budget.model(run)
        self.assertAlmostEqual(sum(charge.values()) / seconds, 40.725, places=3)


class Postures(unittest.TestCase):
    def test_a_parked_device_keys_its_position_and_its_callsign_and_reads_parked(self):
        run = only_run([duty(0), duty(30, tx_keyed_ms=29)])
        _, _, held, _ = power_budget.model(run)
        self.assertEqual(held["parked"], 30)
        self.assertEqual(held["airborne"], 0)

    def test_a_device_keying_every_second_reads_airborne(self):
        run = only_run([duty(0), duty(30, tx_keyed_ms=159)])
        _, _, held, _ = power_budget.model(run)
        self.assertEqual(held["airborne"], 30)
        self.assertEqual(held["parked"], 0)


class Holes(unittest.TestCase):
    def test_an_interval_the_ring_refused_records_inside_is_not_budgeted(self):
        run = only_run([duty(0), gap(15), duty(30, rx_armed_ms=29670)])
        charge, seconds, _, skipped = power_budget.model(run)
        self.assertEqual(seconds, 0)
        self.assertEqual(sum(charge.values()), 0)
        self.assertEqual(skipped, ["the ring refused records inside the interval"])

    def test_a_gap_is_named_before_any_number_is_printed(self):
        run = only_run([duty(0), gap(15, dropped=412), duty(30)])
        report = text(run)
        self.assertIn("412 records the ring had to refuse", report)
        self.assertIn("nothing to budget", report)

    def test_the_interval_a_fix_dated_mid_run_cannot_be_subtracted(self):
        run = only_run([duty(10, flags=PHASE_VALID),
                        duty(1_700_000_000, flags=PHASE_VALID | UTC_DATED)])
        _, seconds, _, skipped = power_budget.model(run)
        self.assertEqual(seconds, 0)
        self.assertEqual(skipped, ["the clock became UTC-dated inside the interval"])

    def test_a_session_the_device_did_not_close_says_so(self):
        run = only_run([duty(0), duty(30)], closed=False)
        self.assertIn("did not close this session", text(run))

    def test_a_run_whose_first_sector_was_recycled_says_so(self):
        run = only_run([duty(0), duty(30)], truncated=True)
        self.assertIn("truncated", text(run))


class Measured(unittest.TestCase):
    def test_a_run_from_full_to_cutoff_spends_the_pack_and_needs_no_curve(self):
        run = only_run([power(0, percent=100), duty(0),
                        power(7200, cell_mv=3201, percent=0, level="cutoff"), duty(7200)])
        cell = power_budget.measure(run, 2400)
        self.assertTrue(cell["whole"])
        self.assertAlmostEqual(cell["mah"], 2400)
        self.assertAlmostEqual(cell["milliamps"], 1200)

    def test_a_partial_run_is_read_off_the_gauge_and_says_that_it_was(self):
        run = only_run([power(0, percent=80), duty(0),
                        power(3600, cell_mv=3800, percent=60), duty(3600)])
        cell = power_budget.measure(run, 2400)
        self.assertFalse(cell["whole"])
        self.assertAlmostEqual(cell["mah"], 480)
        self.assertIn("points of the gauge's own curve", text(run, 2400))

    def test_a_reading_the_sanity_floor_threw_away_is_not_a_measurement(self):
        run = only_run([power(0, flags=PHASE_VALID), duty(0),
                        power(3600, flags=PHASE_VALID), duty(3600)])
        self.assertIsNone(power_budget.measure(run, 2400))
        self.assertIn("no measured draw", text(run, 2400))

    def test_the_residual_is_what_the_table_does_not_explain(self):
        run = only_run([power(0, percent=100), duty(0),
                        power(3600, cell_mv=3201, percent=0, level="cutoff"),
                        duty(3600, rx_armed_ms=3560, tx_keyed_ms=19)])
        report = text(run, 40.0)
        self.assertIn("residual", report)
        self.assertIn("the table explains", report)


class Caveats(unittest.TestCase):
    def test_every_uncited_current_is_counted_out_loud(self):
        cited = sum(1 for consumer in power_budget.CONSUMERS if consumer.cited)
        self.assertEqual(cited, 3)
        report = text(only_run([duty(0), duty(30)]))
        self.assertIn("no current in the table was metered on this board: 3 of 12 cite a "
                      "datasheet, 9 are estimates", report)

    def test_a_row_citing_a_datasheet_names_the_table_it_came_from(self):
        for consumer in power_budget.CONSUMERS:
            self.assertGreater(consumer.milliamps, 0, consumer.name)
            self.assertTrue(consumer.source, consumer.name)
            if consumer.cited:
                self.assertIn("table", consumer.source.lower(), consumer.name)

    def test_a_file_with_no_diagnostics_session_is_an_error_and_not_an_empty_budget(self):
        empty = tempfile.NamedTemporaryFile("w", suffix=".ndjson", delete=False)
        empty.close()
        self.assertEqual(power_budget.main([empty.name]), 1)


class Fields(unittest.TestCase):
    def test_every_duty_field_the_budget_reads_is_a_field_the_decoder_decodes(self):
        decoded = records.decode_diag_record(duty(0))
        for consumer in power_budget.CONSUMERS:
            kind, field, _ = consumer.counter
            if kind != "elapsed":
                self.assertIn(field, decoded)


if __name__ == "__main__":
    unittest.main(verbosity=2)
