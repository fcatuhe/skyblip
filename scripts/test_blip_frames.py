#!/usr/bin/env python3
"""Self-check for blip_frames.py: the CBOR codec and the registry's names and numbers.

    python3 scripts/test_blip_frames.py

Stdlib alone and no radio. Run by test_blip.py as well, which is the file CI invokes.
"""
import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip_frames as frames  # noqa: E402

VECTORS = json.loads((pathlib.Path(__file__).resolve().parent.parent / "schemas"
                      / "link.v1.vectors.json").read_text(encoding="utf-8"))


class Codec(unittest.TestCase):
    # RFC 8949 Appendix A, each value behind the one-entry map {0: value}.
    def test_integers_are_written_in_their_shortest_form(self):
        for value, tail in ((0, "00"), (23, "17"), (24, "1818"), (1000, "1903e8"),
                            (1000000, "1a000f4240"), (2 ** 32 - 1, "1affffffff"),
                            (-1, "20"), (-100, "3863"), (-1000, "3903e7"),
                            (-2 ** 32, "3affffffff")):
            self.assertEqual(frames.encode({0: value}).hex(), "a100" + tail)
            self.assertEqual(frames.decode(bytes.fromhex("a100" + tail)), {0: value})

    def test_text_bytes_booleans_and_arrays_are_written_as_rfc_8949_spells_them(self):
        encoded = frames.encode({1: False, 2: True, 3: "IETF", 4: b"\x01\x02\x03\x04",
                                 5: [1, 2, 3]})
        self.assertEqual(encoded.hex(), "a501f402f503644945544604440102030405830102" "03")

    def test_keys_go_out_ascending_whatever_order_they_were_given_in(self):
        self.assertEqual(frames.encode({3: 1, 1: 2}).hex(), "a2010203" "01")

    def test_an_integer_past_32_bits_is_not_written(self):
        for value in (2 ** 32, -2 ** 32 - 1):
            with self.subTest(value=value), self.assertRaises(frames.FrameError):
                frames.encode({0: value})

    # One grammar for the three readers of the link: the firmware's, blip.py's and the pages'.
    def test_what_the_shared_vectors_say_is_not_a_frame_is_refused(self):
        for vector in VECTORS["not_frames"] + VECTORS["no_command"]:
            with self.subTest(vector["why"]), self.assertRaises(frames.FrameError):
                frames.decode_frame(bytes.fromhex(vector["hex"]))

    def test_what_the_deterministic_subset_does_not_allow_is_refused(self):
        for refused in ("a1001805", "a1180005", "bf0005ff", "a1007f6161ff", "a100f93c00",
                        "a100f6", "a100c105", "a10181a0", "a201050005", "a2010501" "06",
                        "a12005", "a1000500", "a1006261", "a20005"):
            with self.subTest(refused=refused), self.assertRaises(frames.FrameError):
                frames.decode(bytes.fromhex(refused))


class Registry(unittest.TestCase):
    def test_the_widest_status_is_these_twenty_bytes(self):
        frame = {"command": "status", "flight": "airborne", "upload": False,
                 "battery_percent": 100, "charging": False, "power_level": "critical",
                 "went_dark_flat": False, "die_temperature_c": -3277}
        encoded = frames.encode_frame(frame)
        self.assertEqual(encoded.hex(), "a800050102" "02f4031864" "04f40502" "06f407390ccc")
        self.assertEqual(frames.decode_frame(encoded), frame)

    def test_a_set_and_a_defaults_reply_share_one_key_table(self):
        defaults = {"command": "defaults", "aircraft_type": 1, "alarm_enabled": True,
                    "alarm_volume": "loud", "units": "nautical", "callsign": ""}
        as_set = dict(defaults, command="set")
        self.assertEqual(frames.encode_frame(defaults)[3:], frames.encode_frame(as_set)[3:])

    def test_a_byte_string_is_accepted_as_hex_and_read_back_as_bytes(self):
        encoded = frames.encode_frame({"command": "challenge", "nonce": "00" * 16,
                                       "server_key_id": "890026b1"})
        decoded = frames.decode_frame(encoded)
        self.assertEqual(decoded["server_key_id"], bytes.fromhex("890026b1"))
        self.assertEqual(frames.printable(decoded),
                         '{"command":"challenge","nonce":"%s","server_key_id":"890026b1"}'
                         % ("00" * 16))

    # The same refusals as the pages' encoder, so blip.py cannot send what a page would not.
    def test_what_the_shared_vectors_say_is_not_encodable_is_refused_before_anything_is_sent(self):
        for vector in VECTORS["not_encodable"]:
            with self.subTest(vector["why"]), self.assertRaises(frames.FrameError):
                frames.encode_frame(vector["fields"])

    def test_a_name_the_registry_does_not_hold_is_refused_before_anything_is_sent(self):
        for bad in ({"command": "apply"}, {"command": "set", "region": 3},
                    {"command": "set", "units": "imperial"},
                    {"command": "unlock", "serial": 1, "signature": "00"}):
            with self.subTest(bad=bad), self.assertRaises(frames.FrameError):
                frames.encode_frame(bad)

    def test_a_code_the_registry_does_not_hold_stays_a_number_for_a_person_to_read(self):
        self.assertEqual(frames.decode_frame(frames.encode({0: 0, 3: 200, 9: 1})),
                         {"command": "acknowledgement", "reason": 200, "9": 1})

    # Every frame the registry lists survives the round trip, so no table can hold a type the codec drops.
    def test_every_frame_and_every_key_round_trips(self):
        samples = {"unsigned": 70000, "integer": -300, "boolean": True, "text": "D-KXYZ",
                   "unsigned_array": [0, 24, 70000]}
        for command, frame in frames.REGISTRY["frames"].items():
            fields = {"command": command}
            for name, key in frame["by_name"].items():
                if key["type"] == "enum":
                    fields[name] = frames.REGISTRY["enums"][key["enum"]]["values"][-1]["name"]
                elif key["type"] == "bytes":
                    fields[name] = bytes(range(key.get("bytes", 30)))
                else:
                    fields[name] = samples[key["type"]]
            with self.subTest(command=command):
                self.assertEqual(frames.decode_frame(frames.encode_frame(fields)), fields)


if __name__ == "__main__":
    unittest.main(verbosity=2)
