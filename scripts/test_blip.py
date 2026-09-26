#!/usr/bin/env python3
"""Self-check for blip.py and blip_records.py: the decoders, NMEA, and resume.

    python3 scripts/test_blip.py

Stdlib alone and no radio: every record here is handcrafted bytes, laid out
against firmware/core/diag/payload_*.cpp and firmware/core/flight/log_record.cpp.
Those two files and these tables are the same layout written twice, once on each
side of a Bluetooth link, and nothing but a test holds them together.

test_blip_offload.py holds the log endpoint conversation and runs with this file.
"""
import argparse
import base64
import json
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip  # noqa: E402
import blip_records as records  # noqa: E402

SCHEMA = pathlib.Path(__file__).resolve().parents[1] / "schemas" / "diagnostics_log.v1.schema.json"


def diag_record(type_id, payload, flags=0, into_ms=0, at_s=0):
    raw = bytearray(24)
    raw[0] = type_id
    raw[1] = flags
    raw[2:4] = into_ms.to_bytes(2, "little")
    raw[4:8] = at_s.to_bytes(4, "little")
    raw[8:8 + len(payload)] = payload
    return bytes(raw)


def flight_record(fields=b"", flags=0, offset_s=0):
    raw = bytearray(24)
    raw[0:2] = offset_s.to_bytes(2, "little")
    raw[2:2 + len(fields)] = fields
    raw[21] = flags
    raw[22:24] = records.crc16_ccitt(raw[:22]).to_bytes(2, "little")
    return bytes(raw)


class DiagnosticsEnvelope(unittest.TestCase):
    def test_the_envelope_is_type_flags_into_ms_and_at_s(self):
        raw = diag_record(16, b"", flags=0b11, into_ms=1200, at_s=1_700_000_000)
        decoded = records.decode_diag_record(raw)
        self.assertEqual(decoded["type"], "gap")
        self.assertEqual(decoded["into_ms"], 1200)
        self.assertEqual(decoded["at_s"], 1_700_000_000)
        self.assertTrue(decoded["phase_valid"])
        self.assertTrue(decoded["utc_dated"])

    def test_an_all_zero_slot_is_not_a_boot_record(self):
        with self.assertRaises(records.RecordError):
            records.decode_diag_record(bytes(24))

    def test_an_erased_slot_says_erased_rather_than_decoding(self):
        with self.assertRaises(records.RecordError) as caught:
            records.decode_diag_record(b"\xFF" * 24)
        self.assertIn("erased", str(caught.exception))

    def test_a_type_this_decoder_does_not_know_is_refused_not_guessed(self):
        with self.assertRaises(records.RecordError):
            records.decode_diag_record(diag_record(99, b""))

    def test_a_slot_that_is_not_24_bytes_is_refused(self):
        with self.assertRaises(records.RecordError):
            records.decode_diag_record(diag_record(3, b"")[:20])


def decoded(type_id, payload, flags=0):
    return records.decode_diag_record(diag_record(type_id, payload, flags=flags))


def whole(type_name, flags, **fields):
    return dict(type=type_name, at_s=0, into_ms=0, phase_valid=bool(flags & 1),
                utc_dated=bool(flags & 2), **fields)


class DiagnosticsPayloads(unittest.TestCase):
    def test_boot_reads_capabilities_version_reset_and_image_state(self):
        payload = struct.pack("<2IH4B", 0x1FFF, 4242, 7, 1, 2, 5, 1)
        self.assertEqual(decoded(1, payload), whole(
            "boot", 0, capabilities=0x1FFF, fw_build=4242, fw_revision=7, fw_major=1, fw_minor=2,
            reset="WATCHDOG", image_state="probation"))

    def test_config_reads_signed_trims_and_its_three_flag_bits(self):
        payload = struct.pack("<I2h4B2b", 0xABCDEF, -120, -35, 9, 7, 3, 2, -9, 22)
        self.assertEqual(decoded(2, payload, 0b1_1100), whole(
            "config", 0b1_1100, addr=0xABCDEF, battery_offset_mv=-120, freq_trim_e1_ppm=-35,
            aircraft_type=9, addr_table=7, alarm_volume=3, settings_version=2,
            tx_power_dbm=-9, pa_rated_dbm=22, alarm_enabled=True, metric=True,
            battery_trim_manual=True))

    def test_gnss_reads_its_five_words_two_enums_and_five_flags(self):
        payload = struct.pack("<5H5B", 350, 12, 120, 180, 900, 11, 17, 3, 4, 2)
        self.assertEqual(decoded(3, payload, 0b0111_1100), whole(
            "gnss", 0b0111_1100, nav_ms=350, resid_m=12, hdop_e2=120, vdop_e2=180, stage_s=900,
            sats=11, sats_in_view=17, fix_mode=3, reject="STALE", stage="solving", fix_valid=True,
            resid_valid=True, pps_locked=True, geoid_measured=True, tx_settled=True))

    def test_pps_error_is_signed_so_a_slow_edge_reads_negative(self):
        payload = struct.pack("<IiI2H", 999_987, -13, 3600, 2, 450)
        self.assertEqual(decoded(4, payload, 0b1100), whole(
            "pps", 0b1100, interval_us=999_987, error_us=-13, samples=3600, holdover_events=2,
            since_edge_ms=450, locked=True, utc_valid=True))

    def test_burst_reads_the_radio_entry_with_its_signed_rssi_and_key_offset(self):
        payload = struct.pack("<I2H5B2b", 0x0102AB, 410, 960, 8, 2, 1, 1, 26, -97, -2)
        self.assertEqual(decoded(5, payload, 0b0011_1100), whole(
            "burst", 0b0011_1100, addr=0x0102AB, tx_keyed_us=410, tx_span_us=960,
            verdict="miskeyed", source="alptas", band="O", channel=1, len=26, rssi_dbm=-97,
            key_offset_s=-2, addr_valid=True, rssi_valid=True, airborne=True, tx_span_valid=True,
            callsign=False))

    def test_a_transmitted_burst_says_whether_it_carried_the_callsign_or_a_position(self):
        payload = struct.pack("<I2H5B2b", 0x0102AB, 410, 960, 0, 3, 0, 0, 20, 0, 0)
        self.assertTrue(decoded(5, payload, 0b0100_0000)["callsign"])
        self.assertFalse(decoded(5, payload, 0b0000_0000)["callsign"])

    # radio::Event gained Named at 5 in PR #69 and the six verdicts behind it each moved one up.
    def test_the_verdict_behind_received_is_the_callsign_one_and_not_a_bad_crc(self):
        payload = struct.pack("<I2H5B2b", 0x0102AB, 0, 0, 5, 0, 0, 0, 20, -70, 0)
        self.assertEqual(decoded(5, payload)["verdict"], "named")
        payload = struct.pack("<I2H5B2b", 0x0102AB, 0, 0, 6, 0, 0, 0, 20, -70, 0)
        self.assertEqual(decoded(5, payload)["verdict"], "bad_crc")

    def test_dwell_reads_the_slot_map_and_a_refusal(self):
        payload = struct.pack("<I4H3Bb", 868_200_000, 450, 1200, 612, 7, 4, 0, 1, -105)
        self.assertEqual(decoded(6, payload, 0b0100_0100), whole(
            "dwell", 0b0100_0100, freq_hz=868_200_000, start_ms=450, end_ms=1200, phase_ms=612,
            duty_permille=7, state="slot1", band="M", refusal="over_budget", noise_dbm=-105,
            tx_allowed=True, own_tx_dwell=False, listen_only=False, armed=False, burst_armed=True))

    def test_flight_keeps_the_margin_the_state_was_decided_on(self):
        payload = struct.pack("<2ih2H2B", 11_800, -450, 612, 130, 210, 2, 1)
        self.assertEqual(decoded(7, payload, 0b0011_1100), whole(
            "flight", 0b0011_1100, speed_mm_s=11_800, climb_mm_s=-450, alt_msl_m=612, hdop_e2=130,
            vdop_e2=210, declared="airborne", confirmed="on_ground", fix_valid=True, rolling=True,
            climb_valid=True, tx_settled=True))

    def test_power_reads_the_cell_the_die_and_the_saturating_counts(self):
        payload = struct.pack("<4Hh3Bh", 3987, 1, 4, 2, -53, 74, 2, 3, 0)
        self.assertEqual(decoded(8, payload, 0b0011_0100), whole(
            "power", 0b0011_0100, cell_mv=3987, supply_warnings=1, implausible=4,
            charge_warnings=2, die_dc=-53, percent=74, level="low", charge="too_hot",
            trim_offset_mv=0, charging=True, external_power=False, valid=True, die_valid=True,
            caution=False, trim_learned=False))

    def test_power_reads_the_caution_knee_and_the_trim_a_charger_taught_the_unit(self):
        payload = struct.pack("<4Hh3Bh", 3550, 0, 0, 0, 210, 18, 1, 1, -40)
        self.assertEqual(decoded(8, payload, 0b1101_0100), whole(
            "power", 0b1101_0100, cell_mv=3550, supply_warnings=0, implausible=0,
            charge_warnings=0, die_dc=210, percent=18, level="normal", charge="ok",
            trim_offset_mv=-40, charging=True, external_power=False, valid=True, die_valid=False,
            caution=True, trim_learned=True))

    def test_baro_altitude_and_climb_are_signed_millimetres(self):
        payload = struct.pack("<I2ih", 95_432_100, -1234, -2500, -104)
        self.assertEqual(decoded(9, payload, 0b0001_1100), whole(
            "baro", 0b0001_1100, pressure_mpa=95_432_100, alt_mm=-1234, climb_mm_s=-2500,
            temperature_dc=-104, active=True, temperature_valid=True, climb_adopted=True))

    def test_motion_reads_six_signed_axes_and_two_error_bytes(self):
        payload = struct.pack("<6h2B", -120, 1010, -35, 44, 2600, -800, 0x11, 0x22)
        self.assertEqual(decoded(10, payload, 0b0001_1100), whole(
            "motion", 0b0001_1100, slip_mg=-120, normal_mg=1010, lateral_mg=-35,
            longitudinal_mg=44, most_normal_mg=2600, least_normal_mg=-800, imu_error=0x11,
            sensor_error=0x22, slip_valid=True, gload_valid=True, fitted=True))

    def test_contact_keeps_the_edge_instant_and_how_long_the_level_had_held(self):
        payload = struct.pack("<2I2B", 123_456, 1040, 1, 2)
        self.assertEqual(decoded(11, payload, 0b0100), whole(
            "contact", 0b0100, at_ms=123_456, held_ms=1040, contact="pad", gesture=2, down=True))

    def test_link_names_the_action_the_endpoint_and_the_negotiated_payload(self):
        payload = struct.pack("<5H2B", 3, 182, 96, 3, 5, 2, 2)
        self.assertEqual(decoded(12, payload, 0b0100), whole(
            "link", 0b0100, session=3, payload_bytes=182, frame_bytes=96, holder=3, drops=5,
            action="claim_taken", endpoint="log", claim_held=True))

    def test_traffic_reads_the_encounter_geometry_with_signed_altitude(self):
        payload = struct.pack("<I2H2h2BbB", 0xDD1234, 2450, 270, -180, 32, 1, 1, -88, 12)
        self.assertEqual(decoded(13, payload, 0b0010_0100), whole(
            "traffic", 0b0010_0100, addr=0xDD1234, dist_m=2450, bearing_deg=270, rel_alt_m=-180,
            closing_mps=32, alarm=1, source="adsl_uplink", rssi_dbm=-88, tracked=12, assessed=True,
            dismissed=False, in_formation=False, position_valid=True))

    def test_write_reads_the_placement_verdict_and_what_it_cost(self):
        payload = struct.pack("<I4H2B", 2900, 612, 40, 9, 1, 3, 1)
        self.assertEqual(decoded(14, payload, 0b0100), whole(
            "write", 0b0100, waited_ms=2900, phase_ms=612, requests=40, writes=9, forced=1,
            placement="forced", kind="flight_record", pending=True))

    def test_screen_reads_the_page_codes_the_product_owns(self):
        payload = struct.pack("<I4B", 2500, 6, 1, 4, 1)
        self.assertEqual(decoded(15, payload, 0b0001_1100), whole(
            "screen", 0b0001_1100, since_ms=2500, page=6, mode=1, prompt=4, alarm=1,
            backlight=True, powered=True, holding=True))

    def test_gap_carries_the_hole_itself(self):
        payload = struct.pack("<3IH", 17, 830, 41, 64)
        self.assertEqual(decoded(16, payload), whole(
            "gap", 0, dropped=17, span_ms=830, total=41, capacity=64))

    def test_end_carries_the_session_its_own_account_of_itself(self):
        payload = struct.pack("<2I", 512, 3)
        self.assertEqual(decoded(17, payload), whole(
            "end", 0, records=512, dropped=3))

    def test_duty_reads_seven_counters_out_of_fourteen_bytes(self):
        payload = struct.pack("<7H", 1904, 37, 41_250, 58_300, 1420, 22_700, 640)
        self.assertEqual(decoded(18, payload), whole(
            "duty", 0, panel_partial_refreshes=1904, panel_full_refreshes=37,
            backlight_ms=41_250, rx_armed_ms=58_300, tx_keyed_ms=1420, ble_connected_ms=22_700,
            annunciator_ms=640))

    # The bytes firmware/test/core/diag/test_diag_decided.cpp pins the encoder to, 30 s apart.
    def test_two_duty_records_a_wrap_apart_subtract_to_the_true_interval(self):
        before = records.decode_diag_record(bytes.fromhex(
            "1202000040f9a16a00000000e8fd60ea0000000000000000"))
        after = records.decode_diag_record(bytes.fromhex(
            "120200005ef9a16a000000001873905f0000000000000000"))
        self.assertEqual(after["at_s"] - before["at_s"], 30)
        self.assertEqual((before["backlight_ms"], after["backlight_ms"]), (65_000, 29_464))
        self.assertEqual((after["backlight_ms"] - before["backlight_ms"]) & 0xFFFF, 30_000)
        self.assertEqual((after["rx_armed_ms"] - before["rx_armed_ms"]) & 0xFFFF, 30_000)


    # The bytes firmware/test/core/diag/test_diag_decided.cpp pins the encoder to.
    def test_a_config_record_names_the_transmitter_it_was_captured_on(self):
        decoded = records.decode_diag_record(bytes.fromhex(
            "0202000040f9a16afeca5b0000000000000700010e160000"))
        self.assertEqual(decoded["addr"], 0x5BCAFE)
        self.assertEqual(decoded["tx_power_dbm"], 14)
        self.assertEqual(decoded["pa_rated_dbm"], 22)


class TablesAgainstTheSchema(unittest.TestCase):
    def setUp(self):
        self.schema = json.loads(SCHEMA.read_text(encoding="utf-8"))

    def test_the_type_numbers_are_the_schema_enum_in_the_same_order(self):
        named = [name for name, _ in
                 (records.DIAG_TYPES[key] for key in sorted(records.DIAG_TYPES))]
        self.assertEqual(named, self.schema["properties"]["type"]["enum"])
        self.assertEqual(sorted(records.DIAG_TYPES), list(range(1, 19)))

    def test_every_enum_tuple_is_the_schema_enum_in_the_same_order(self):
        for field, names in records.enum_fields():
            self.assertEqual(list(names), self.schema["properties"][field]["enum"], field)

    def test_every_schema_enum_has_a_tuple_to_decode_its_ordinal_through(self):
        decoded_through_names = {field for field, _ in records.enum_fields()}
        for field, spec in self.schema["properties"].items():
            if field == "type" or "enum" not in spec:
                continue
            self.assertIn(field, decoded_through_names, field)

    def test_every_decoded_key_is_a_key_the_schema_declares(self):
        allowed = set(self.schema["properties"])
        for type_id in records.DIAG_TYPES:
            decoded = records.decode_diag_record(diag_record(type_id, bytes(range(16))))
            self.assertLessEqual(set(decoded), allowed, "type %d" % type_id)

    def test_no_field_reads_past_the_sixteen_byte_payload(self):
        for type_id, (name, fields) in records.DIAG_TYPES.items():
            short = diag_record(type_id, bytes(16))[:23] + b"\x00"
            decoded = records.decode_diag_record(short)
            self.assertEqual(decoded["type"], name)
            self.assertEqual(len(decoded), len(fields) + 5)


class GoldenVectors(unittest.TestCase):
    """Bytes the firmware's own encoders produced, so the two layouts are pinned to each other."""

    def test_a_gnss_record_encoded_by_the_firmware(self):
        decoded = records.decode_diag_record(
            bytes.fromhex("037fb00400f153655e010c007800b40084030b1103040200"))
        self.assertEqual(decoded["at_s"], 1_700_000_000)
        self.assertEqual(decoded["into_ms"], 1200)
        self.assertEqual(decoded["nav_ms"], 350)
        self.assertEqual(decoded["stage_s"], 900)
        self.assertEqual(decoded["reject"], "STALE")
        self.assertEqual(decoded["stage"], "solving")

    def test_a_burst_record_encoded_by_the_firmware(self):
        decoded = records.decode_diag_record(
            bytes.fromhex("053fb00400f15365ab0201009a01c003080201011a9ffe00"))
        self.assertEqual(decoded["addr"], 0x0102AB)
        self.assertEqual(decoded["verdict"], "miskeyed")
        self.assertEqual(decoded["rssi_dbm"], -97)
        self.assertEqual(decoded["key_offset_s"], -2)

    def test_a_pps_record_encoded_by_the_firmware(self):
        decoded = records.decode_diag_record(
            bytes.fromhex("040fb00400f1536533420f00f3ffffff100e00000200c201"))
        self.assertEqual(decoded["interval_us"], 999_987)
        self.assertEqual(decoded["error_us"], -13)
        self.assertEqual(decoded["samples"], 3600)

    def test_a_flight_fix_encoded_by_the_firmware(self):
        decoded = records.decode_flight_record(
            bytes.fromhex("d20400cf511c7929edffb004d090008000f0ff090ccf0209"), 1_600_000_000)
        self.assertEqual(decoded["utc"], 1_600_001_234)
        self.assertEqual(decoded["lat_1e7"], 475_123_456)
        self.assertEqual(decoded["lon_1e7"], -1_234_567)
        self.assertEqual(decoded["alt_hae_m"], 1152)
        self.assertEqual(decoded["track_deg"], 90)
        self.assertEqual(decoded["flight_state"], "airborne")
        self.assertTrue(decoded["session_end"])


class FlightRecords(unittest.TestCase):
    def test_the_crc_is_ccitt_over_the_first_twenty_two_bytes(self):
        self.assertEqual(records.crc16_ccitt(b"123456789"), 0x31C3)

    def test_a_torn_record_is_refused_rather_than_handed_back_as_a_position(self):
        raw = bytearray(flight_record(offset_s=30))
        raw[5] ^= 0xFF
        with self.assertRaises(records.RecordError) as caught:
            records.decode_flight_record(bytes(raw), 1_700_000_000)
        self.assertEqual(str(caught.exception), "crc")

    def test_an_erased_slot_is_not_a_fix_at_the_epoch(self):
        with self.assertRaises(records.RecordError):
            records.decode_flight_record(b"\xFF" * 24, 1_700_000_000)

    def test_utc_is_the_session_base_plus_the_stored_offset(self):
        decoded = records.decode_flight_record(flight_record(offset_s=1234), 1_700_000_000)
        self.assertEqual(decoded["utc"], 1_700_001_234)

    def test_position_speed_track_and_climb_come_back_in_their_own_units(self):
        fields = struct.pack("<2ihb2Hh2B", 475_123_456, -1_234_567, 1200, -48, 144, 128, -16, 9, 12)
        decoded = records.decode_flight_record(flight_record(fields), 0)
        self.assertEqual(decoded["lat_1e7"], 475_123_456)
        self.assertEqual(decoded["lon_1e7"], -1_234_567)
        self.assertEqual(decoded["alt_msl_m"], 1200)
        self.assertEqual(decoded["alt_hae_m"], 1152)
        self.assertEqual(decoded["speed_q"], 144)
        self.assertEqual(decoded["speed_mm_s"], 36_000)
        self.assertEqual(decoded["track_c9"], 128)
        self.assertEqual(decoded["track_deg"], 90)
        self.assertEqual(decoded["climb_e8"], -16)
        self.assertEqual(decoded["climb_mm_s"], -2000)
        self.assertEqual(decoded["sats"], 9)
        self.assertEqual(decoded["hdop_e2"], 120)

    def test_the_flight_state_rides_in_bits_five_and_six_of_the_flag_byte(self):
        airborne = records.decode_flight_record(flight_record(flags=0b0100_0000), 0)
        self.assertEqual(airborne["flight_state"], "airborne")
        self.assertFalse(airborne["session_end"])
        landed = records.decode_flight_record(flight_record(flags=0b1010_1111), 0)
        self.assertEqual(landed["flight_state"], "on_ground")
        self.assertTrue(landed["session_end"])
        self.assertTrue(landed["fix_valid"])
        self.assertTrue(landed["utc_valid"])
        self.assertTrue(landed["pps_locked"])
        self.assertTrue(landed["climb_valid"])
        self.assertFalse(landed["geoid_measured"])


class Chunks(unittest.TestCase):
    def test_a_chunk_numbers_its_records_from_the_index_it_was_asked_for(self):
        blob = base64.b64encode(diag_record(16, (1).to_bytes(4, "little"))
                                + diag_record(4, bytes(16))).decode()
        decoded = records.decode_chunk("diagnostics", blob, 0, 24)
        self.assertEqual([record["index"] for record in decoded], [24, 25])
        self.assertEqual([record["type"] for record in decoded], ["gap", "pps"])

    def test_an_unreadable_slot_stays_in_the_stream_as_a_hole(self):
        blob = base64.b64encode(diag_record(99, bytes(16))).decode()
        decoded = records.decode_chunk("diagnostics", blob, 0, 7)
        self.assertEqual(decoded[0]["type"], "unreadable")
        self.assertEqual(decoded[0]["index"], 7)
        self.assertIn("unknown type 99", decoded[0]["error"])

    def test_a_chunk_that_is_not_whole_records_is_refused(self):
        with self.assertRaises(ValueError):
            records.decode_chunk("diagnostics", base64.b64encode(b"\x01" * 30).decode(), 0, 0)

    def test_a_flights_chunk_is_decoded_against_the_session_base(self):
        blob = base64.b64encode(flight_record(offset_s=5)).decode()
        decoded = records.decode_chunk("flights", blob, 1_600_000_000, 0)
        self.assertEqual(decoded[0]["utc"], 1_600_000_005)


class Summaries(unittest.TestCase):
    def corpus(self):
        return [
            {"index": 0, "type": "gnss", "at_s": 100, "utc_dated": True},
            {"index": 1, "type": "gap", "at_s": 101, "utc_dated": True, "dropped": 9,
             "span_ms": 300, "total": 9, "capacity": 64},
            {"index": 4, "type": "gnss", "at_s": 160, "utc_dated": True},
        ]

    def test_the_summary_counts_types_and_names_the_span(self):
        lines = records.summarise(self.corpus())
        self.assertIn("3 records", lines[0])
        self.assertTrue(any("gnss         2" in line for line in lines))
        self.assertTrue(any(line.startswith("span ") and line.endswith(", 60 s")
                            for line in lines))

    def test_a_gap_record_is_printed_in_full_because_a_hole_must_be_visible(self):
        lines = records.summarise(self.corpus())
        self.assertTrue(any("9 records dropped over 300 ms" in line for line in lines))

    def test_indices_missing_from_the_file_are_reported_as_missing(self):
        self.assertIn("missing indices 2..3", records.summarise(self.corpus()))

    def test_a_corpus_with_no_hole_says_so_rather_than_staying_quiet(self):
        whole = [{"index": 0, "type": "pps", "at_s": 10, "utc_dated": False}]
        self.assertIn("no gap records and no unreadable slots", records.summarise(whole))

    def test_a_boot_relative_record_is_not_rendered_as_a_1970_date(self):
        text = records.stamp_of({"at_s": 42, "into_ms": 7, "utc_dated": False})
        self.assertEqual(text, "+42s 7ms")

    def test_the_summary_names_the_end_markers_own_account_of_the_session(self):
        corpus = self.corpus() + [
            {"index": 5, "type": "end", "at_s": 161, "utc_dated": True,
             "records": 4, "dropped": 9}]
        lines = records.summarise(corpus)
        self.assertTrue(any("4 records written, 9 dropped" in line for line in lines), lines)


class Nmea(unittest.TestCase):
    def test_a_known_sentence_checksum(self):
        self.assertEqual(blip.nmea_checksum("PFLAU,2,1,2,1,0,45,0,120,2345,ABCDEF"), "7B")

    def test_a_sentence_carrying_the_wrong_checksum_is_caught(self):
        self.assertTrue(blip.nmea_checksum_ok("$PFLAU,0,1,2*51"))
        self.assertFalse(blip.nmea_checksum_ok("$PFLAU,0,1,2*52"))

    def test_a_line_with_no_checksum_at_all_does_not_pass(self):
        self.assertFalse(blip.nmea_checksum_ok("$PFLAU,0,1,2"))
        self.assertFalse(blip.nmea_checksum_ok("PFLAU,0,1,2*51"))

    def test_a_lowercase_checksum_from_a_sloppy_talker_still_matches(self):
        self.assertTrue(blip.nmea_checksum_ok("$PFLAU,2,1,2,1,0,45,0,120,2345,ABCDEF*7b"))


class Reassembly(unittest.TestCase):
    def test_a_notification_cut_mid_sentence_yields_nothing_until_the_rest_arrives(self):
        assembler = blip.LineAssembler()
        self.assertEqual(assembler.feed(b"$PFLAU,0,1"), [])
        self.assertEqual(assembler.feed(b",2*51\r\n"), ["$PFLAU,0,1,2*51"])

    def test_several_sentences_in_one_notification_come_out_in_order(self):
        assembler = blip.LineAssembler()
        self.assertEqual(assembler.feed(b"$A*41\r\n$B*42\r\n"), ["$A*41", "$B*42"])

    def test_a_bare_newline_ends_a_line_as_well_as_a_crlf(self):
        self.assertEqual(blip.LineAssembler().feed(b"$A*41\n$B*42\n"), ["$A*41", "$B*42"])

    def test_the_tail_of_a_burst_is_held_for_the_next_notification(self):
        assembler = blip.LineAssembler()
        assembler.feed(b"$A*41\r\n$B")
        self.assertEqual(assembler.held, "$B")
        self.assertEqual(assembler.feed(b"*42\r\n"), ["$B*42"])

    def test_a_blank_line_between_sentences_is_not_a_sentence(self):
        self.assertEqual(blip.LineAssembler().feed(b"\r\n$A*41\r\n"), ["$A*41"])


def fetch_args(**overrides):
    defaults = dict(log="flights", session=None, all=False, out=None, restart=False)
    defaults.update(overrides)
    return argparse.Namespace(**defaults)


SESSIONS = [
    {"session": 100, "records": 50, "closed": True},
    {"session": 200, "records": 30, "closed": True},
    {"session": 300, "records": 10, "closed": False},
]


class Resume(unittest.TestCase):
    def test_the_last_line_of_a_fetched_file_is_where_the_next_fetch_starts(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "capture.ndjson"
            path.write_text("".join(
                json.dumps({"log": "flights", "session": 200, "index": index}) + "\n"
                for index in range(3)), encoding="utf-8")
            self.assertEqual(records.resume_point(str(path)), (200, 2))

    def test_a_file_truncated_mid_line_resumes_from_the_last_whole_record(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "capture.ndjson"
            path.write_text('{"session": 200, "index": 0}\n{"session": 200, "ind',
                            encoding="utf-8")
            self.assertEqual(records.resume_point(str(path)), (200, 0))

    def test_no_file_yet_means_no_resume_point(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertIsNone(records.resume_point(str(pathlib.Path(directory) / "absent")))

    def test_without_a_resume_point_every_chosen_session_starts_at_zero(self):
        plan = blip.sessions_to_fetch(SESSIONS, fetch_args(all=True), None)
        self.assertEqual([(entry["session"], start) for entry, start in plan],
                         [(100, 0), (200, 0), (300, 0)])

    def test_a_fetch_that_died_mid_session_restarts_after_the_last_index_it_kept(self):
        plan = blip.sessions_to_fetch(SESSIONS, fetch_args(all=True), (200, 17))
        self.assertEqual([(entry["session"], start) for entry, start in plan],
                         [(200, 18), (300, 0)])

    def test_a_session_already_complete_is_not_fetched_again(self):
        plan = blip.sessions_to_fetch(SESSIONS, fetch_args(all=True), (200, 29))
        self.assertEqual([(entry["session"], start) for entry, start in plan], [(300, 0)])

    def test_a_resume_point_naming_a_session_the_device_no_longer_lists_starts_over(self):
        plan = blip.sessions_to_fetch(SESSIONS, fetch_args(all=True), (999, 4))
        self.assertEqual([(entry["session"], start) for entry, start in plan],
                         [(100, 0), (200, 0), (300, 0)])

    def test_the_default_is_the_last_session_on_the_device(self):
        plan = blip.sessions_to_fetch(SESSIONS, fetch_args(), None)
        self.assertEqual([entry["session"] for entry, _ in plan], [300])

    def test_one_session_asked_for_by_id_is_the_only_one_fetched(self):
        plan = blip.sessions_to_fetch(SESSIONS, fetch_args(session=100), None)
        self.assertEqual([entry["session"] for entry, _ in plan], [100])


class ReplyFraming(unittest.TestCase):
    def test_a_diag_frame_saying_more_is_not_the_last_one(self):
        self.assertTrue(blip.more_follows('{"cmd":"diag","group":"radio","part":0,"more":true}'))
        self.assertFalse(blip.more_follows('{"cmd":"diag","group":"screen","part":4,"more":false}'))

    def test_a_reply_with_no_more_field_is_a_single_frame(self):
        self.assertFalse(blip.more_follows('{"cmd":"status","ack":true}'))

    def test_a_frame_that_is_not_json_ends_the_collection_rather_than_hanging(self):
        self.assertFalse(blip.more_follows("<truncated"))

    def test_a_refused_command_raises_with_the_reason_the_device_gave(self):
        with self.assertRaises(SystemExit) as caught:
            blip.refuse_if_nacked({"cmd": "log", "ack": False, "reason": "in_flight"})
        self.assertIn("in_flight", str(caught.exception))

    def test_an_acknowledged_reply_passes_through_untouched(self):
        frame = {"cmd": "log", "ack": True, "sessions": 2}
        self.assertIs(blip.refuse_if_nacked(frame), frame)


def load_tests(loader, tests, pattern):
    """CI runs this file by name, so the offload suite is loaded here rather than left unrun."""
    tests.addTests(loader.loadTestsFromName("test_blip_offload"))
    return tests


if __name__ == "__main__":
    unittest.main(verbosity=2)
