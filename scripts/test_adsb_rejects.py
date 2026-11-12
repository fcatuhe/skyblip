#!/usr/bin/env python3
"""Self-check for adsb_rejects.py: a known DF17, damaged the ways the README asks about.

    python3 scripts/test_adsb_rejects.py

Stdlib alone. The sync, the generator and the console line are read off the firmware, so a
change there fails here rather than decoding every dump wrong.
"""
import contextlib
import io
import pathlib
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import adsb_rejects  # noqa: E402

FIRMWARE = pathlib.Path(__file__).resolve().parents[1] / "firmware"
# The DF17 identification of KLM1023 that every Mode S primer decodes.
SOUND = bytes.fromhex("8D4840D6202CC371C32CE0576098")


def stream_of(frame):
    stream = []
    for byte in frame:
        for i in range(8):
            stream.extend((1, 0) if byte >> (7 - i) & 1 else (0, 1))
    return stream


def line_of(stream, status="crc", rssi="-87"):
    chips = stream[8:adsb_rejects.STREAM_CHIPS]
    packed = bytes(int("".join(map(str, chips[i:i + 8])), 2) for i in range(0, len(chips), 8))
    return ("[00:01:02.345,000] <dbg> skyblip: log_adsb_rejects: "
            "adsb reject status=%s rssi=%s chips=%s" % (status, rssi, packed.hex()))


def reading_of(line):
    return adsb_rejects.read(adsb_rejects.LINE.search(line))


class Reading(unittest.TestCase):
    def test_a_sound_frame_has_no_residue_and_names_its_aircraft(self):
        reading = reading_of(line_of(stream_of(SOUND), status="unsupported"))
        self.assertEqual((reading.df, reading.icao, reading.type_code), (17, 0x4840D6, 4))
        self.assertEqual((reading.residue, reading.doubtful), (0, []))
        self.assertEqual((reading.flip, reading.shift, reading.slip), (None, None, []))

    def test_one_wrong_bit_is_found_and_said_not_doubtful(self):
        stream = stream_of(SOUND)
        stream[2 * 59], stream[2 * 59 + 1] = stream[2 * 59 + 1], stream[2 * 59]
        reading = reading_of(line_of(stream))
        self.assertNotEqual(reading.residue, 0)
        self.assertEqual(reading.flip, 60)
        self.assertNotIn(60, reading.doubtful)

    def test_a_pair_of_equal_chips_is_doubtful_where_it_sits(self):
        stream = stream_of(SOUND)
        stream[2 * 99] = stream[2 * 99 + 1]
        reading = reading_of(line_of(stream))
        self.assertEqual(reading.doubtful, [100])
        self.assertIn("doubtful=1 at 100", adsb_rejects.describe(reading))

    def test_an_extra_chip_inside_the_frame_is_found_where_it_landed(self):
        stream = stream_of(SOUND)
        stream.insert(150, 1 - stream[149])
        reading = reading_of(line_of(stream))
        self.assertNotEqual(reading.residue, 0)
        self.assertIn((150, 1), reading.slip)
        self.assertIsNone(reading.shift)

    def test_a_chip_lost_after_the_sync_is_a_whole_frame_shift(self):
        stream = stream_of(SOUND)
        del stream[8]
        stream.append(0)
        reading = reading_of(line_of(stream))
        self.assertEqual(reading.shift, -1)


class Summary(unittest.TestCase):
    def test_a_console_log_reads_into_one_line_a_frame_and_a_summary(self):
        late = stream_of(SOUND)
        late[2 * 99] = late[2 * 99 + 1]
        log = "\n".join([line_of(stream_of(SOUND), status="unsupported"), "radio rx_ok=3",
                         line_of(late), ""])
        with tempfile.NamedTemporaryFile("w", suffix=".log") as console:
            console.write(log)
            console.flush()
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                self.assertEqual(adsb_rejects.main(["adsb_rejects.py", console.name]), 0)
        lines = out.getvalue().splitlines()
        self.assertEqual(lines[0],
                         "unsupported rssi=-87 df=17 icao=4840d6 tc=4 residue=000000 doubtful=0")
        self.assertEqual(lines[2], "frames 2: crc 1, unsupported 1")
        self.assertIn("4840d6 (2)", lines[4])
        self.assertTrue(lines[-1].endswith(": 0 0 0 1"))


class Firmware(unittest.TestCase):
    def test_the_sync_and_the_generator_are_the_firmware_s(self):
        adsb = (FIRMWARE / "core" / "protocol" / "adsb.h").read_text()
        crc = (FIRMWARE / "core" / "fec" / "crc.h").read_text()
        self.assertIn("kAdsbSyncChips = 0x%02X;" % adsb_rejects.SYNC_CHIPS, adsb)
        self.assertIn("kPolynomial = 0x%08Xu;" % (adsb_rejects.GENERATOR << 7), crc)

    def test_the_example_in_bravo_s_readme_reads_as_it_says(self):
        readme = (FIRMWARE / "products" / "skyblip_bravo" / "README.md").read_text()
        logged, printed = re.search(r"\n(adsb reject [^\n]*)\n([^\n]*)\n", readme).groups()
        self.assertEqual(adsb_rejects.describe(reading_of(logged)), printed)

    def test_every_line_main_cpp_logs_is_a_line_it_reads(self):
        main = (FIRMWARE / "products" / "skyblip_bravo" / "main.cpp").read_text()
        formats = re.findall(r'LOG_DBG\("(adsb reject [^"]*)"', main)
        self.assertEqual(len(formats), 2)
        for logged in formats:
            line = logged.replace("%s", "crc", 1).replace("%d", "-87").replace("%s", "00" * 27)
            self.assertIsNotNone(adsb_rejects.LINE.search(line), logged)


if __name__ == "__main__":
    unittest.main()
