#!/usr/bin/env python3
"""Self-check for the altitude half of blip_records.py: the ADS-B record and the unknown altitude.

    python3 scripts/test_blip_altitude.py

test_blip.py loads this file, so CI runs it under that name. The hex strings are the
bytes the firmware's host suite pins its encoders to, so the two layouts are held to
each other: firmware/test/products/test_adsb_capture.cpp,
firmware/test/core/diagnostics/test_diagnostics_decided.cpp and
firmware/test/core/test_log_events.cpp.
"""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip_records as records  # noqa: E402

BASE_UTC_S = 1_785_628_800


def adsb_record(payload, flags):
    raw = bytearray(24)
    raw[0] = 21
    raw[1] = flags
    raw[8:8 + len(payload)] = payload
    return records.decode_diagnostics_record(bytes(raw))


class AdsbRecord(unittest.TestCase):
    # A rig's DF17 frames, folded, recorded, drained to its flash and read back.
    def test_a_host_capture_decodes_to_the_target_the_rig_heard(self):
        decoded = records.decode_diagnostics_record(
            bytes.fromhex("15ffc80083886e6a5e4d3c12b80b0000edfeb004e5017a02"))
        self.assertEqual(decoded, {
            "type": "adsb", "at_s": BASE_UTC_S + 3, "into_ms": 200, "phase_valid": True,
            "utc_dated": True, "address": 0x3C4D5E, "adsb_version": 2, "gva": 2,
            "pressure_altitude_ft": 3000, "height_difference_ft": -275,
            "height_difference_age_ms": 1200, "gnss_relative_altitude_m": 485,
            "pressure_relative_altitude_m": 634, "adsb_version_valid": True, "gva_valid": True,
            "pressure_altitude_valid": True, "height_difference_valid": True,
            "gnss_relative_altitude_valid": True, "pressure_relative_altitude_valid": True})

    def test_a_version_never_heard_is_absent_and_version_zero_is_zero(self):
        payload = bytes([0x5E, 0x4D, 0x3C, 0x00])
        unheard = adsb_record(payload, 0)
        self.assertNotIn("adsb_version", unheard)
        self.assertFalse(unheard["adsb_version_valid"])
        heard = adsb_record(payload, 0b0000_0100)
        self.assertEqual(heard["adsb_version"], 0)
        self.assertTrue(heard["adsb_version_valid"])

    def test_an_altitude_neither_side_could_compare_is_absent_rather_than_zero(self):
        payload = struct.pack("<3sBihhhh", bytes([0x5E, 0x4D, 0x3C]), 0x01, 2600, 0, 0, 0, 0)
        decoded = adsb_record(payload, 0b0001_0100)
        self.assertEqual(decoded["pressure_altitude_ft"], 2600)
        self.assertEqual(decoded["adsb_version"], 1)
        for absent in ("gva", "height_difference_ft", "height_difference_age_ms",
                       "gnss_relative_altitude_m", "pressure_relative_altitude_m"):
            self.assertNotIn(absent, decoded)
        self.assertFalse(decoded["gnss_relative_altitude_valid"])
        self.assertFalse(decoded["pressure_relative_altitude_valid"])


class UnknownAltitude(unittest.TestCase):
    def test_a_capture_target_with_no_altitude_decodes_as_unknown(self):
        decoded = records.decode_diagnostics_record(
            bytes.fromhex("0d66000040f9a16a5e4d3c00c8050c00000000000104b903"))
        self.assertEqual(decoded["type"], "traffic")
        self.assertEqual(decoded["source"], "adsb")
        self.assertFalse(decoded["relative_altitude_valid"])
        self.assertNotIn("relative_altitude_m", decoded)

    def test_a_capture_target_from_before_the_flag_keeps_its_altitude_and_claims_nothing(self):
        payload = struct.pack("<I2H2h2BbB", 0xDD1234, 2450, 270, -180, 32, 1, 1, -88, 12)
        raw = bytearray(24)
        raw[0] = 13
        raw[1] = 0b0010_0100
        raw[8:24] = payload
        decoded = records.decode_diagnostics_record(bytes(raw))
        self.assertEqual(decoded["relative_altitude_m"], -180)
        self.assertNotIn("relative_altitude_valid", decoded)

    def test_a_flight_log_encounter_with_no_altitude_decodes_as_unknown(self):
        decoded = records.decode_flight_record(
            bytes.fromhex("2c013412dd06b40000003b010c0001028300a8045f0041c9"), BASE_UTC_S)
        self.assertEqual(decoded["type"], "encounter")
        self.assertFalse(decoded["relative_altitude_valid"])
        self.assertNotIn("relative_altitude_m", decoded)
        self.assertEqual(decoded["distance_m"], 180)

    # The encounter test_log_events.cpp pinned before the flag: an old flight still decodes.
    def test_a_flight_log_encounter_from_before_the_flag_keeps_its_altitude(self):
        decoded = records.decode_flight_record(
            bytes.fromhex("2c013412dd06b400d6ff3b010c0001028300a8005f002eb1"), BASE_UTC_S)
        self.assertEqual(decoded["relative_altitude_m"], -42)
        self.assertNotIn("relative_altitude_valid", decoded)
        self.assertEqual(decoded["heard_s"], 95)


if __name__ == "__main__":
    unittest.main(verbosity=2)
