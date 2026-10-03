#!/usr/bin/env python3
"""Self-check for blip.py's side of the link: NMEA, the replies it collects, and resume.

    python3 scripts/test_blip_link.py

Stdlib alone and no radio. Run by test_blip.py as well, which is the file CI invokes.
"""
import argparse
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip  # noqa: E402
import blip_records as records  # noqa: E402


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

    def test_a_session_left_unfinished_before_a_new_one_opened_is_finished_first(self):
        plan = blip.sessions_to_fetch(SESSIONS, fetch_args(), (200, 17))
        self.assertEqual([(entry["session"], start) for entry, start in plan],
                         [(200, 18), (300, 0)])

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





if __name__ == "__main__":
    unittest.main(verbosity=2)
