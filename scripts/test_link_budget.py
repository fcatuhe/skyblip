#!/usr/bin/env python3
"""Self-check for link_budget.py: the pairing, the free-space arithmetic, and what it sets.

    python3 scripts/test_link_budget.py

Stdlib alone, no radio and no clock. Every record here is handcrafted bytes put
through the same chunk decoder and session sink blip.py fetch uses, so a Burst
or Config field that moves in firmware/core/diag/payload.h fails here rather
than reading as a quiet zero.
"""
import base64
import contextlib
import io
import json
import math
import pathlib
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip_records as records  # noqa: E402
import link_budget  # noqa: E402

FIRMWARE = pathlib.Path(__file__).resolve().parents[1] / "firmware"
SX1262_H = FIRMWARE / "hardware" / "parts" / "sx1262" / "sx1262.h"
SLOT_H = FIRMWARE / "core" / "timing" / "slot.h"

CONFIG = 2
BURST = 5
TRAFFIC = 13
GAP = 16

PHASE_VALID = 0x01
UTC_DATED = 0x02
ADDR_VALID = 0x04
RSSI_VALID = 0x08
POSITION_VALID = 0x20
DATED = PHASE_VALID | UTC_DATED

UNIT_A = 0x5BCAFE
UNIT_B = 0x5BCB00
STRANGER = 0x3F0001

FIX_AT = 1_789_000_000

VERDICTS = {name: code for code, name in enumerate(records.VERDICT)}

QW = "ANT-868-CW-QW-SMA"


def slot(type_id, payload, flags, at_s, into_ms=0):
    raw = bytearray(24)
    raw[0] = type_id
    raw[1] = flags
    raw[2:4] = into_ms.to_bytes(2, "little")
    raw[4:8] = at_s.to_bytes(4, "little")
    raw[8:8 + len(payload)] = payload
    return bytes(raw)


def config(addr, power_dbm=14, pa_rated_dbm=22):
    payload = bytearray(16)
    payload[0:4] = addr.to_bytes(4, "little")
    payload[12] = power_dbm & 0xFF
    payload[13] = pa_rated_dbm & 0xFF
    return slot(CONFIG, bytes(payload), DATED, FIX_AT)


def burst(verdict, at_s, into_ms, addr=0, channel=0, rssi_dbm=0, flags=DATED):
    payload = bytearray(16)
    payload[0:4] = addr.to_bytes(4, "little")
    payload[8] = VERDICTS[verdict]
    payload[11] = channel
    payload[13] = rssi_dbm & 0xFF
    if addr:
        flags |= ADDR_VALID
    if verdict not in link_budget.OWN_VERDICTS:
        flags |= RSSI_VALID
    return slot(BURST, bytes(payload), flags, at_s, into_ms)


def sent(at_s, into_ms=462, channel=0, flags=DATED):
    return burst("transmitted", at_s, into_ms, channel=channel, flags=flags)


def heard(sender, at_s, into_ms=463, rssi_dbm=-40, channel=0, verdict="received", flags=DATED):
    return burst(verdict, at_s, into_ms, addr=sender, channel=channel, rssi_dbm=rssi_dbm,
                 flags=flags)


def traffic(addr, at_s, dist_m):
    payload = bytearray(16)
    payload[0:4] = addr.to_bytes(4, "little")
    payload[4:6] = dist_m.to_bytes(2, "little")
    return slot(TRAFFIC, bytes(payload), DATED | POSITION_VALID, at_s)


def gap(at_s, dropped=12):
    payload = bytearray(16)
    payload[0:4] = dropped.to_bytes(4, "little")
    return slot(GAP, bytes(payload), DATED, at_s)


def session_lines(raws, session=1):
    """What blip.py fetch writes for one session: the chunk decoder, then the session sink."""
    encoded = base64.b64encode(b"".join(raws)).decode("ascii")
    lines = []
    sink = records.SessionSink("diagnostics", {"session": session, "records": len(raws),
                                               "closed": True, "truncated": False}, lines.append)
    for record in records.decode_chunk("diagnostics", encoded, 0, 0):
        sink.write(record)
    sink.finish(complete=True)
    return lines


def exchange(seconds, rssi_a_to_b=-40, rssi_b_to_a=-40):
    """Two units ten seconds apart in phase, each hearing every burst the other sends."""
    a = [config(UNIT_A)]
    b = [config(UNIT_B)]
    for at in range(FIX_AT, FIX_AT + seconds, 10):
        a.append(sent(at, 462))
        b.append(heard(UNIT_A, at, 463, rssi_a_to_b))
        b.append(sent(at, 812, channel=1))
        a.append(heard(UNIT_B, at, 813, rssi_b_to_a, channel=1))
    return a, b


def ceiling_of(rssi_dbm, distance_m=16.7, height_m=1.2):
    """The upper bound of a link heard at one steady level, where sampling adds nothing."""
    weakest, _ = link_budget.ground_band(distance_m, height_m)
    return expected_erp(rssi_dbm, distance_m) - weakest + link_budget.spread(link_budget.ERP)


def expected_erp(rssi_dbm, distance_m):
    level = rssi_dbm - link_budget.RSSI_TRUNCATION_DB
    receive_chain = link_budget.ANTENNAS[QW].gain_dbi - link_budget.FEED_LOSS_DB
    return level + link_budget.free_space_db(distance_m) - receive_chain - link_budget.DBI_TO_DBD_DB


class Case(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.folder = pathlib.Path(folder.name)

    def capture(self, name, raws):
        path = self.folder / name
        path.write_text("".join(json.dumps(line) + "\n" for line in session_lines(raws)),
                        encoding="utf-8")
        return str(path)

    def sides(self, raws_a, raws_b):
        return (link_budget.Side("a", self.capture("a.ndjson", raws_a), QW),
                link_budget.Side("b", self.capture("b.ndjson", raws_b), QW))

    def text(self, raws_a, raws_b, distance_m=16.7, height_m=1.2):
        lines = []
        a, b = self.sides(raws_a, raws_b)
        link_budget.report(a, b, distance_m, height_m, lines.append)
        return "\n".join(lines)

    def link(self, raws_a, raws_b):
        a, b = self.sides(raws_a, raws_b)
        return link_budget.pair(a, UNIT_A, b)


class Pairing(Case):
    def test_a_burst_heard_a_millisecond_after_it_was_sent_is_one_pair(self):
        link = self.link([config(UNIT_A), sent(FIX_AT)], [heard(UNIT_A, FIX_AT, rssi_dbm=-52)])
        self.assertEqual((link["sent"], link["levels"], link["unmatched"]), (1, [-52], 0))

    def test_a_reception_outside_the_window_pairs_with_nothing(self):
        link = self.link([sent(FIX_AT, 462)], [heard(UNIT_A, FIX_AT, 462 + 51)])
        self.assertEqual((link["levels"], link["unmatched"]), ([], 1))

    def test_a_reception_on_the_other_channel_is_not_that_burst(self):
        link = self.link([sent(FIX_AT, channel=0)], [heard(UNIT_A, FIX_AT, channel=1)])
        self.assertEqual(link["levels"], [])

    def test_a_third_emitter_heard_at_the_same_instant_is_not_the_sender(self):
        link = self.link([sent(FIX_AT)], [heard(STRANGER, FIX_AT)])
        self.assertEqual((link["levels"], link["unmatched"]), ([], 0))

    def test_a_registration_burst_pairs_like_a_position(self):
        link = self.link([sent(FIX_AT)], [heard(UNIT_A, FIX_AT, verdict="named", rssi_dbm=-47)])
        self.assertEqual(link["levels"], [-47])

    def test_a_slot_one_burst_dated_past_a_thousand_ms_pairs_across_the_second(self):
        link = self.link([sent(FIX_AT, 1150)], [heard(UNIT_A, FIX_AT + 1, 151)])
        self.assertEqual(len(link["levels"]), 1)

    def test_one_reception_is_never_counted_for_two_transmissions(self):
        link = self.link([sent(FIX_AT, 462), sent(FIX_AT, 470)], [heard(UNIT_A, FIX_AT, 466)])
        self.assertEqual((link["sent"], len(link["levels"])), (2, 1))

    def test_an_instant_no_pps_edge_measured_is_counted_and_never_paired(self):
        link = self.link([sent(FIX_AT, flags=UTC_DATED), sent(FIX_AT + 10)],
                         [heard(UNIT_A, FIX_AT, flags=UTC_DATED), heard(UNIT_A, FIX_AT + 10)])
        self.assertEqual((link["sent"], link["undated"], link["undated_heard"]), (1, 1, 1))
        self.assertEqual(len(link["levels"]), 1)


class FreeSpace(Case):
    def test_free_space_at_the_quiet_distance_for_one_point_two_metres(self):
        # 20 log10(4 pi 16.7 m 868.3 MHz / c) = 20 log10(607.8)
        self.assertAlmostEqual(link_budget.free_space_db(16.7), 55.675, places=2)

    def test_doubling_the_distance_costs_six_decibels(self):
        self.assertAlmostEqual(link_budget.free_space_db(40) - link_budget.free_space_db(20),
                               20 * math.log10(2), places=9)

    def test_at_the_quiet_distance_the_ground_can_only_strengthen_the_path(self):
        weakest, strongest = link_budget.ground_band(link_budget.quiet_distance_m(1.2), 1.2)
        self.assertAlmostEqual(weakest, 0.0, places=6)
        self.assertGreater(strongest, 5.5)

    def test_where_the_reflection_arrives_a_whole_wave_late_it_can_all_but_cancel(self):
        wavelength = link_budget.SPEED_OF_LIGHT_M_S / link_budget.MBAND_HZ
        # hypot(d, 2h) - d = one wavelength, solved for d
        whole_wave_m = (4 * 1.2 ** 2 - wavelength ** 2) / (2 * wavelength)
        weakest, _ = link_budget.ground_band(whole_wave_m, 1.2)
        self.assertLess(weakest, -20)


class Estimate(Case):
    def test_the_erp_is_what_was_heard_plus_the_path_less_the_receiving_antenna(self):
        a, b = exchange(600, rssi_a_to_b=-40)
        side_a, side_b = self.sides(a, b)
        tx_a, _ = link_budget.transmitter(side_a)
        result = link_budget.direction(link_budget.pair(side_a, UNIT_A, side_b), tx_a,
                                       link_budget.ANTENNAS[QW], link_budget.ANTENNAS[QW],
                                       16.7, 1.2)
        self.assertAlmostEqual(result["erp"], expected_erp(-40, 16.7), places=9)
        self.assertAlmostEqual(result["erp_chain"], 12.95, places=9)

    def test_a_path_heard_at_the_chains_own_level_reads_as_free_space(self):
        chain = 14 - 0.5 + 1.6 + 1.6 - 0.5
        rssi = round(chain - link_budget.free_space_db(16.7) + link_budget.RSSI_TRUNCATION_DB)
        a, b = exchange(600, rssi_a_to_b=rssi)
        side_a, side_b = self.sides(a, b)
        tx_a, _ = link_budget.transmitter(side_a)
        result = link_budget.direction(link_budget.pair(side_a, UNIT_A, side_b), tx_a,
                                       link_budget.ANTENNAS[QW], link_budget.ANTENNAS[QW],
                                       16.7, 1.2)
        self.assertLess(abs(result["excess"]), 0.5)

    def test_the_conducted_nominal_is_the_lower_of_the_power_asked_and_the_pa_row(self):
        side, _ = self.sides([config(UNIT_A, power_dbm=22, pa_rated_dbm=14)], [])
        tx, _ = link_budget.transmitter(side)
        self.assertEqual(tx["conducted_dbm"], 14)

    def test_both_directions_are_printed_with_their_delivery(self):
        a, b = exchange(600)
        b.remove(heard(UNIT_A, FIX_AT, 463, -40))
        report = self.text(a, b)
        self.assertIn("a -> b\n  sent      60 bursts asking for 14 dBm through the +22 dBm PA row",
                      report)
        self.assertIn("heard     59 of them (98.3%)", report)
        self.assertIn("b -> a", report)

    def test_the_gnss_distance_is_printed_beside_the_tape(self):
        a, b = exchange(60)
        report = self.text(a, b + [traffic(UNIT_A, FIX_AT, 18), traffic(UNIT_A, FIX_AT + 1, 16)])
        self.assertIn("b puts a 17 m away, the median of 2 traffic records", report)


class Sets(Case):
    def test_a_link_whose_ceiling_clears_the_limit_keeps_the_programmed_power(self):
        a, b = exchange(600, rssi_a_to_b=-48, rssi_b_to_a=-48)
        self.assertRegex(self.text(a, b), r"kConductedDbm +14 +holds")

    def test_a_link_whose_ceiling_passes_the_limit_brings_the_power_down(self):
        a, b = exchange(600, rssi_a_to_b=-38, rssi_b_to_a=-48)
        ceiling = ceiling_of(-38)
        report = self.text(a, b)
        self.assertRegex(report, r"kConductedDbm +14 +comes down to %d"
                         % math.floor(14 - (ceiling - 14)))

    def test_a_unit_radiating_less_than_the_chain_leaves_a_feed_loss_it_may_claim(self):
        a, b = exchange(600, rssi_a_to_b=-52, rssi_b_to_a=-52)
        ceiling = ceiling_of(-52)
        loss = round((14 + 1.6 - 2.15 - ceiling) * 100)
        self.assertGreater(loss, 0)
        self.assertIn("kFeedLossCentiDb  %d " % loss, self.text(a, b))

    def test_a_link_heard_near_the_chain_claims_no_feed_loss(self):
        a, b = exchange(600, rssi_a_to_b=-40, rssi_b_to_a=-40)
        self.assertIn("kFeedLossCentiDb  0 ", self.text(a, b))

    def test_two_units_are_never_taken_for_the_line(self):
        a, b = exchange(600)
        self.assertIn("a third unit at the top of TXACC would reach", self.text(a, b))

    def test_a_geometry_the_ground_can_weaken_names_the_one_it_cannot(self):
        a, b = exchange(600)
        self.assertIn("at 1.2 m up, 16.7 m apart it cannot", self.text(a, b, distance_m=10.0))
        self.assertNotIn("lifted that ceiling", self.text(a, b, distance_m=16.68))


class Refusals(Case):
    def test_a_capture_with_no_config_cannot_say_who_sent_what(self):
        report = self.text([sent(FIX_AT)], [config(UNIT_B)])
        self.assertIn("holds no config record", report)

    def test_a_capture_from_before_the_transmitter_fields_is_refused(self):
        report = self.text([config(UNIT_A, power_dbm=0, pa_rated_dbm=0)], [config(UNIT_B)])
        self.assertIn("predates the transmitter fields", report)

    def test_a_link_nobody_heard_across_says_so_and_sets_nothing(self):
        report = self.text([config(UNIT_A), sent(FIX_AT)], [config(UNIT_B)])
        self.assertIn("no burst crossed this way", report)
        self.assertNotIn("what it sets", report)

    def test_a_gap_is_named_before_any_number_is_printed(self):
        a, b = exchange(60)
        report = self.text(a, b + [gap(FIX_AT + 30, dropped=412)])
        self.assertLess(report.index("412 records the ring had to refuse"), report.index("a -> b"))

    def test_the_uncertainty_terms_say_how_many_are_estimates(self):
        a, b = exchange(60)
        self.assertIn("1 of 6 uncertainty terms cite a datasheet, 5 are estimates",
                      self.text(a, b))

    def test_an_antenna_with_no_gain_on_file_is_refused_at_the_command_line(self):
        path = self.capture("a.ndjson", [config(UNIT_A)])
        with self.assertRaises(SystemExit) as refused, contextlib.redirect_stderr(io.StringIO()):
            link_budget.main([path, path, "--distance", "16.7", "--height", "1.2",
                              "--antenna-a", QW, "--antenna-b", "stock whip"])
        self.assertEqual(refused.exception.code, 2)

    def test_a_distance_of_nothing_is_refused_at_the_command_line(self):
        path = self.capture("a.ndjson", [config(UNIT_A)])
        for distance in ("0", "-3", "nan"):
            with self.assertRaises(SystemExit) as refused, \
                    contextlib.redirect_stderr(io.StringIO()):
                link_budget.main([path, path, "--distance", distance, "--height", "1.2",
                                  "--antenna-a", QW, "--antenna-b", QW])
            self.assertEqual(refused.exception.code, 2, distance)

    def test_a_refused_capture_exits_non_zero(self):
        a = self.capture("a.ndjson", [sent(FIX_AT)])
        b = self.capture("b.ndjson", [config(UNIT_B)])
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(link_budget.main([a, b, "--distance", "16.7", "--height", "1.2",
                                               "--antenna-a", QW, "--antenna-b", QW]), 1)


class Firmware(Case):
    def constant(self, path, name):
        found = re.search(r"%s = (-?\d+);" % name, path.read_text(encoding="utf-8"))
        self.assertIsNotNone(found, "%s moved out of %s" % (name, path))
        return int(found.group(1))

    def test_the_limit_and_the_dipole_step_are_the_ones_sx1262_h_asserts_against(self):
        self.assertEqual(link_budget.LIMIT_ERP_DBM, self.constant(SX1262_H, "kSrd868ErpLimitDbm"))
        self.assertAlmostEqual(link_budget.DBI_TO_DBD_DB * 100,
                               self.constant(SX1262_H, "kDbiToDbdCentiDb"))

    def test_the_chain_this_compares_against_is_the_one_sx1262_h_takes(self):
        self.assertAlmostEqual(link_budget.FEED_LOSS_DB * 100,
                               self.constant(SX1262_H, "kFeedLossCentiDb"))
        self.assertAlmostEqual(link_budget.ANTENNAS[QW].gain_dbi * 100,
                               self.constant(SX1262_H, "kAntennaPeakGainDbiCentiDb"))

    def test_the_frequency_is_midway_between_the_two_m_band_channels(self):
        channels = (self.constant(SLOT_H, "kMband0Hz"), self.constant(SLOT_H, "kMband1Hz"))
        self.assertEqual(link_budget.MBAND_HZ, sum(channels) // 2)

    def test_every_field_the_link_reads_is_a_field_the_decoder_decodes(self):
        for raw, fields in ((config(UNIT_A), ("addr", "tx_power_dbm", "pa_rated_dbm")),
                            (heard(UNIT_A, FIX_AT), ("addr", "verdict", "band", "channel",
                                                     "rssi_dbm", "addr_valid", "rssi_valid")),
                            (traffic(UNIT_A, FIX_AT, 17), ("addr", "dist_m", "position_valid"))):
            decoded = records.decode_diag_record(raw)
            for field in fields:
                self.assertIn(field, decoded)

    def test_a_term_citing_a_datasheet_names_the_table_it_came_from(self):
        for term in link_budget.TERMS:
            self.assertGreater(term.db, 0, term.name)
            self.assertTrue(term.source, term.name)
            if term.cited:
                self.assertIn("table", term.source.lower(), term.name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
