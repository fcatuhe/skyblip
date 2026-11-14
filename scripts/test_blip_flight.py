#!/usr/bin/env python3
"""Self-check for the flight log half of blip_records.py: the fix and event records.

    python3 scripts/test_blip_flight.py

test_blip.py loads this file, so CI runs it under that name. The records are handcrafted
bytes laid out against firmware/core/flight/log_record.cpp.
"""
import json
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip_records as records  # noqa: E402

FLIGHT_SCHEMA = (pathlib.Path(__file__).resolve().parents[1] / "schemas"
                 / "flight_log.v1.schema.json")


def flight_record(fields=b"", flags=0, offset_s=0):
    raw = bytearray(24)
    raw[0:2] = offset_s.to_bytes(2, "little")
    raw[2:2 + len(fields)] = fields
    raw[21] = flags
    raw[22:24] = records.crc16_ccitt(raw[:22]).to_bytes(2, "little")
    return bytes(raw)


def event_record(kind, fields=b"", flags=0xFF, offset_s=0):
    raw = bytearray(24)
    raw[0:2] = offset_s.to_bytes(2, "little")
    raw[2:2 + len(fields)] = fields
    raw[records.FLIGHT_KIND_OFFSET] = records.FLIGHT_EVENT_MARK | kind
    raw[records.FLIGHT_EVENTS[kind][1]] = flags
    raw[22:24] = records.crc16_ccitt(raw[:22]).to_bytes(2, "little")
    return bytes(raw)


class FlightLogAgainstTheSchema(unittest.TestCase):
    def setUp(self):
        self.schema = json.loads(FLIGHT_SCHEMA.read_text(encoding="utf-8"))

    def decoded_samples(self):
        yield records.decode_flight_record(flight_record(flags=0xFF), 0)
        for kind in records.FLIGHT_EVENTS:
            yield records.decode_flight_record(event_record(kind), 0)

    def test_the_types_are_the_schema_enum_in_the_order_of_their_kind(self):
        named = ["fix"] + [records.FLIGHT_EVENTS[kind][0] for kind in sorted(records.FLIGHT_EVENTS)]
        self.assertEqual(named, self.schema["properties"]["type"]["enum"])
        self.assertEqual(sorted(records.FLIGHT_EVENTS), list(range(1, len(named))))

    def test_every_enum_tuple_is_the_schema_enum_in_the_same_order(self):
        for field, names in records.flight_enum_fields():
            self.assertEqual(list(names), self.schema["properties"][field]["enum"], field)

    def test_the_keys_decoded_are_the_keys_the_schema_declares(self):
        allowed = set(self.schema["properties"])
        seen = set()
        for decoded in self.decoded_samples():
            self.assertLessEqual(set(decoded), allowed, decoded["type"])
            seen |= set(decoded)
        self.assertEqual(seen, allowed)

    def test_no_decoded_key_collides_with_the_keys_a_fetch_wraps_each_record_in(self):
        for decoded in self.decoded_samples():
            self.assertEqual(set(decoded) & {"log", "session", "index"}, set(), decoded["type"])


class FlightRecords(unittest.TestCase):
    def test_the_crc_is_ccitt_over_the_first_twenty_two_bytes(self):
        self.assertEqual(records.crc16_ccitt(b"123456789"), 0x31C3)

    def test_a_torn_record_is_refused_rather_than_handed_back_as_a_position(self):
        raw = bytearray(flight_record(offset_s=30))
        raw[5] ^= 0xFF
        with self.assertRaises(records.RecordError) as caught:
            records.decode_flight_record(bytes(raw), 1_700_000_000)
        self.assertEqual(str(caught.exception), "crc")

    def test_an_event_is_told_from_a_fix_by_the_top_bit_of_byte_sixteen(self):
        fix = records.decode_flight_record(flight_record(struct.pack("<13xH", 511)), 0)
        self.assertEqual(fix["type"], "fix")
        self.assertEqual(fix["track_c9"], 511)
        alarm = records.decode_flight_record(event_record(2, flags=0), 0)
        self.assertEqual(alarm["type"], "alarm")
        self.assertNotIn("latitude_1e7", alarm)

    def test_an_event_kind_this_decoder_does_not_know_is_unreadable_not_guessed(self):
        raw = bytearray(event_record(1))
        raw[records.FLIGHT_KIND_OFFSET] = records.FLIGHT_EVENT_MARK | 0x7F
        raw[22:24] = records.crc16_ccitt(raw[:22]).to_bytes(2, "little")
        with self.assertRaises(records.RecordError) as caught:
            records.decode_flight_record(bytes(raw), 0)
        self.assertEqual(str(caught.exception), "unknown event kind 127")

    def test_an_erased_slot_is_not_a_fix_at_the_epoch(self):
        with self.assertRaises(records.RecordError):
            records.decode_flight_record(b"\xFF" * 24, 1_700_000_000)

    def test_utc_is_the_session_base_plus_the_stored_offset(self):
        decoded = records.decode_flight_record(flight_record(offset_s=1234), 1_700_000_000)
        self.assertEqual(decoded["utc_s"], 1_700_001_234)

    def test_position_speed_track_and_climb_come_back_in_their_own_units(self):
        fields = struct.pack("<2ihb2Hh2B", 475_123_456, -1_234_567, 1200, -48, 144, 128, -16, 9, 12)
        decoded = records.decode_flight_record(flight_record(fields), 0)
        self.assertEqual(decoded["latitude_1e7"], 475_123_456)
        self.assertEqual(decoded["longitude_1e7"], -1_234_567)
        self.assertEqual(decoded["altitude_msl_m"], 1200)
        self.assertEqual(decoded["altitude_hae_m"], 1152)
        self.assertEqual(decoded["speed_q"], 144)
        self.assertEqual(decoded["speed_mm_s"], 36_000)
        self.assertEqual(decoded["track_c9"], 128)
        self.assertEqual(decoded["track_deg"], 90)
        self.assertEqual(decoded["climb_e8"], -16)
        self.assertEqual(decoded["climb_mm_s"], -2000)
        self.assertEqual(decoded["satellites"], 9)
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
