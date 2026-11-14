#!/usr/bin/env python3
"""Self-check for adsb_trial.py and the trial's two records. Temporary: #293 removes it.

    python3 scripts/test_adsb_trial.py

Stdlib alone. The record bytes are the ones firmware/test/core/diagnostics/
test_diagnostics_adsb_trial.cpp pins the encoder to.
"""
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import adsb_trial  # noqa: E402
import blip_records as records  # noqa: E402

# The DF17 identification of KLM1023 that every Mode S primer decodes.
SOUND = bytes.fromhex("8D4840D6202CC371C32CE0576098")


def chips_of(frame, doubtful=None, flipped=None):
    """The 27 chip bytes behind the 0x95 sync, with one bit's pair doubled or turned over."""
    stream = []
    for bit in range(len(frame) * 8):
        one = frame[bit >> 3] >> (7 - (bit & 7)) & 1
        pair = [1, 0] if one else [0, 1]
        if bit + 1 == doubtful:
            pair = [pair[0], pair[0]]
        if bit + 1 == flipped:
            pair = pair[::-1]
        stream.extend(pair)
    stream = stream[8:]
    return bytes(int("".join(map(str, stream[i:i + 8])), 2) for i in range(0, len(stream), 8))


def slot(profile, threshold, positions, session=1, counted=29, manchester=False):
    return {"type": "adsb_slot", "session": session, "trial_profile": profile,
            "threshold_dbm": threshold, "gain_step": 0, "boost": 0, "frames": 300, "crc": 200,
            "unsupported": 10, "folded": 2 * positions, "positions": positions, "aircraft": 5,
            "counted_s": counted, "front_end_raw": True, "manchester": manchester,
            "fifo_dropped": False}


def halves(seq, chips, verdict="crc"):
    return [{"type": "adsb_chips", "seq": seq, "rssi_dbm": -80, "rssi_valid": True,
             "chips": chips[:14].hex(), "tail": False, "chips_verdict": verdict},
            {"type": "adsb_chips", "seq": seq, "rssi_dbm": -80, "rssi_valid": True,
             "chips": (chips[14:] + b"\0").hex(), "tail": True, "chips_verdict": verdict}]


class Records(unittest.TestCase):
    def test_a_slot_decodes_to_its_profile_and_its_counts(self):
        decoded = records.decode_diagnostics_record(
            bytes.fromhex("161bce0140f9a16a019e0d0794021c020c0062002900093b"))
        self.assertEqual(decoded["type"], "adsb_slot")
        self.assertEqual((decoded["trial_profile"], decoded["threshold_dbm"], decoded["gain_step"],
                          decoded["boost"]), (1, -98, 13, 7))
        self.assertEqual((decoded["frames"], decoded["crc"], decoded["unsupported"],
                          decoded["folded"], decoded["positions"]), (660, 540, 12, 98, 41))
        self.assertEqual((decoded["aircraft"], decoded["counted_s"]), (9, 59))
        self.assertEqual((decoded["front_end_raw"], decoded["manchester"],
                          decoded["fifo_dropped"]), (False, True, True))

    def test_a_head_of_chips_decodes_to_its_bytes_level_and_verdict(self):
        decoded = records.decode_diagnostics_record(
            bytes.fromhex("171bce0140f9a16a07a9a0a1a2a3a4a5a6a7a8a9aaabacad"))
        self.assertEqual(decoded["type"], "adsb_chips")
        self.assertEqual((decoded["seq"], decoded["rssi_dbm"], decoded["chips"]),
                         (7, -87, "a0a1a2a3a4a5a6a7a8a9aaabacad"))
        self.assertEqual((decoded["tail"], decoded["rssi_valid"], decoded["chips_verdict"]),
                         (False, True, "crc"))


class Slots(unittest.TestCase):
    def test_each_profile_is_rated_over_its_slots(self):
        lines = [slot(0, -90, 30), slot(1, -98, 60), slot(1, -98, 66), slot(0, -90, 36)]
        out = adsb_trial.report(lines, "positions")
        # 66 positions over 58 counted seconds is 68.3 a minute.
        self.assertIn("-90 dBm, gain agc, boost 0, 0x18, chips: 2, 1.0, 620.7, 66.7, 136.6, 68.3, "
                      "5.0, 0", out)

    def test_adjacent_slots_compare_two_profiles_and_say_which_was_ahead(self):
        lines = [slot(0, -90, 29), slot(1, -98, 58), slot(1, -98, 58), slot(0, -90, 29)]
        out = adsb_trial.report(lines, "positions")
        self.assertIn("A -90 dBm, gain agc, boost 0, 0x18, chips | B -98 dBm, gain agc, boost 0, "
                      "0x18, chips: 2, +60.0, +60.0 to +60.0, 2 of 2", out)

    def test_a_short_slot_and_a_new_session_break_the_pairs(self):
        lines = [slot(0, -90, 29), slot(1, -98, 5, counted=5), slot(0, -90, 29),
                 slot(1, -98, 58, session=2)]
        out = adsb_trial.report(lines, "positions")
        self.assertIn("no two adjacent slots ran different profiles, so nothing compares", out)

    def test_a_capture_with_no_slot_says_so(self):
        self.assertEqual(adsb_trial.report([{"type": "boot"}], "positions"),
                         ["no adsb_slot record: did a capture run beside the trial, "
                          "and was it fetched?"])


class Replay(unittest.TestCase):
    def test_each_policy_accepts_what_it_repairs_and_no_more(self):
        lines = (halves(1, chips_of(SOUND), "decoded") + halves(2, chips_of(SOUND, doubtful=61))
                 + halves(3, chips_of(SOUND, flipped=61)) + [slot(0, -90, 29)])
        frames = adsb_trial.frames_of(lines)
        self.assertEqual([frame[1] for frame in frames], ["decoded", "crc", "crc"])
        out = adsb_trial.replay_report(frames, slips=False)
        self.assertIn("-90 dBm, gain agc, boost 0, 0x18, chips: 3, 1, 2, 3", out)
        self.assertIn("addresses only flip finds, never in a sound frame: none", out)

    def test_a_tail_without_its_head_is_no_frame_and_a_frame_past_the_last_slot_is_open(self):
        lines = halves(1, chips_of(SOUND))[1:] + halves(2, chips_of(SOUND))
        frames = adsb_trial.frames_of(lines)
        self.assertEqual([frame[0] for frame in frames], ["open slot"])

    def test_the_cli_reads_what_blip_fetch_writes(self):
        lines = [{"type": "session", "session": 1}] + halves(1, chips_of(SOUND), "decoded") + [
            slot(0, -90, 29)]
        with tempfile.NamedTemporaryFile("w", suffix=".ndjson", delete=False) as out:
            out.write("".join(json.dumps(line) + "\n" for line in lines))
        self.assertEqual(len(adsb_trial.frames_of(adsb_trial.read(out.name))), 1)
        pathlib.Path(out.name).unlink()


if __name__ == "__main__":
    unittest.main(verbosity=2)
