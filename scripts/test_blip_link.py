#!/usr/bin/env python3
"""Self-check for blip.py's side of the link: NMEA, the replies it collects, and resume.

    python3 scripts/test_blip_link.py

Stdlib alone and no radio. Run by test_blip.py as well, which is the file CI invokes.
"""
import argparse
import asyncio
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip  # noqa: E402
import blip_capture  # noqa: E402
import blip_frames as frames  # noqa: E402
import blip_protocols  # noqa: E402
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


def from_the_unit(fields):
    return frames.encode_frame(fields)


class ReplyFraming(unittest.TestCase):
    def test_a_diagnostics_frame_saying_more_is_not_the_last_one(self):
        self.assertTrue(frames.more_follows(blip.decoded_or_refused(from_the_unit(
            {"command": "diagnostics", "group": "radio", "part": 0, "more": True}))))
        self.assertFalse(frames.more_follows(blip.decoded_or_refused(from_the_unit(
            {"command": "diagnostics", "group": "power", "part": 4, "more": False}))))

    def test_a_reply_with_no_more_field_is_a_single_frame(self):
        self.assertFalse(frames.more_follows(blip.decoded_or_refused(from_the_unit(
            {"command": "acknowledgement", "acknowledge": True}))))

    def test_a_frame_outside_the_registry_stops_the_collection_rather_than_hanging(self):
        with self.assertRaises(SystemExit) as caught:
            blip.decoded_or_refused(b"<truncated")
        self.assertIn("3c7472756e6361746564", str(caught.exception))

    # A key 0 that is an array used to escape as a TypeError traceback, not a refusal.
    def test_a_frame_whose_command_is_not_a_code_stops_the_collection_by_name(self):
        for data in (bytes.fromhex("a1008100"), bytes.fromhex("a10161ff")):
            with self.subTest(data=data.hex()), self.assertRaises(SystemExit) as caught:
                blip.decoded_or_refused(data)
            self.assertIn(data.hex(), str(caught.exception))

    # A typed command the registry refuses is said before connecting, never a traceback.
    def test_a_typed_command_the_registry_refuses_is_refused_by_name(self):
        for text in ('{"command":"set","units":"imperial"}', '{"command":"challenge","nonce":"zz"}',
                     '{"command":["set"]}', '["set"]', '{"command":'):
            with self.subTest(text=text), self.assertRaises(SystemExit) as caught:
                blip.command_fields(text)
            self.assertIn(text, str(caught.exception))

    def test_a_typed_command_the_registry_allows_is_handed_on_as_typed(self):
        self.assertEqual(blip.command_fields('{"command":"set","units":"metric"}'),
                         {"command": "set", "units": "metric"})

    def test_a_refused_command_raises_with_the_reason_the_device_gave(self):
        with self.assertRaises(SystemExit) as caught:
            blip.refuse_if_nacked({"command": "log", "acknowledge": False, "reason": "in_flight"})
        self.assertIn("in_flight", str(caught.exception))

    # A refusal for width names what the pilot can do, not only the reason's code.
    def test_a_reply_refused_as_too_wide_says_the_link_kept_its_smallest_packet(self):
        for command in ("log", "acknowledgement"):
            with self.subTest(command=command), self.assertRaises(SystemExit) as caught:
                blip.refuse_if_nacked({"command": command, "acknowledge": False, "reason": "small_mtu"})
            self.assertIn("smallest packet size, 20 bytes", str(caught.exception))

    def test_an_acknowledged_reply_passes_through_untouched(self):
        frame = {"command": "log", "acknowledge": True, "sessions": 2}
        self.assertIs(blip.refuse_if_nacked(frame), frame)


class FakeBravo:
    """The config endpoint of a unit that stages the prompt, then gives the presses' verdict."""

    def __init__(self, *answers):
        self.replies = asyncio.Queue()
        for answer in answers:
            self.replies.put_nowait((0.0, frames.encode_frame(answer)))
        self.sent = []

    async def write_config(self, body):
        frames.encode_frame(body)
        self.sent.append(body)


PENDING = {"command": "acknowledgement", "acknowledge": False, "pending": True,
           "reason": "confirm_capture"}
GRANTED = {"command": "acknowledgement", "acknowledge": True, "reason": "capture"}
PUSHED = {"command": "status", "flight": "ground", "upload": False, "charging": False,
          "power_level": "normal", "went_dark_flat": False}


class Capture(unittest.TestCase):
    def test_two_presses_on_the_unit_arm_the_profile_asked_for(self):
        unit = FakeBravo(PENDING, GRANTED)
        said = []
        outcome = asyncio.run(blip_capture.run(unit, "power_run", 1.0, said.append))
        self.assertEqual(unit.sent, [{"command": "capture", "profile": "power_run"}])
        self.assertEqual(said, [blip_capture.PRESS_TWICE])
        self.assertEqual(outcome, "power_run capture armed")

    # The unit pushes status on a flight, battery or Bravo change, which is no verdict.
    def test_a_status_pushed_while_the_prompt_stands_is_passed_over(self):
        unit = FakeBravo(PUSHED, PENDING, PUSHED, GRANTED)
        said = []
        outcome = asyncio.run(blip_capture.run(unit, "full", 1.0, said.append))
        self.assertEqual(said, [blip_capture.PRESS_TWICE])
        self.assertEqual(outcome, "full capture armed")

    def test_a_frame_outside_the_registry_stops_by_name_rather_than_hanging(self):
        unit = FakeBravo()
        unit.replies.put_nowait((0.0, b"<truncated"))
        with self.assertRaises(SystemExit) as caught:
            asyncio.run(blip_capture.run(unit, "full", 1.0, list().append, blip.decoded_or_refused))
        self.assertIn("3c7472756e6361746564", str(caught.exception))

    def test_a_stop_allowed_on_the_unit_says_the_capture_stopped(self):
        outcome = asyncio.run(blip_capture.run(FakeBravo(PENDING, GRANTED), "stop", 1.0, list().append))
        self.assertEqual(outcome, "capture stopped")

    def test_one_press_on_the_unit_stops_with_the_reason(self):
        cancelled = {"command": "acknowledgement", "acknowledge": False, "reason": "cancelled"}
        with self.assertRaises(SystemExit) as caught:
            asyncio.run(blip_capture.run(FakeBravo(PENDING, cancelled), "full", 1.0, list().append))
        self.assertIn("cancelled", str(caught.exception))

    def test_a_go_answers_unknown_command_and_nothing_waits_for_a_press(self):
        unknown = {"command": "acknowledgement", "acknowledge": False, "reason": "unknown_command"}
        said = []
        with self.assertRaises(SystemExit) as caught:
            asyncio.run(blip_capture.run(FakeBravo(unknown), "full", 1.0, said.append))
        self.assertIn("unknown_command", str(caught.exception))
        self.assertEqual(said, [])

    def test_a_prompt_nobody_answers_ends_without_a_verdict(self):
        with self.assertRaises(SystemExit) as caught:
            asyncio.run(blip_capture.run(FakeBravo(PENDING), "full", 0.01, list().append))
        self.assertIn("no verdict", str(caught.exception))

    def test_the_profiles_offered_are_the_registry_s(self):
        self.assertEqual(blip_capture.PROFILES, ("full", "power_run", "flight_run", "stop"))


GO_PROTOCOLS = {"command": "protocols", "adsl_version": "0.1.0",
                "adsl_specification": "ADS-L 4 SRD-860 Issue 2", "alptas_version": "0.2.0",
                "alptas_specification": "ALP-TAS 2024 FLARM-generation frame"}


def protocol_lines(*answers):
    unit = FakeBravo(*answers)
    frame = asyncio.run(blip_protocols.answer(unit, 1.0))
    return unit.sent, blip_protocols.lines(frame, blip.refuse_if_nacked)


class Protocols(unittest.TestCase):
    def test_a_go_names_each_stack_its_version_and_what_it_implements(self):
        sent, lines = protocol_lines(GO_PROTOCOLS)
        self.assertEqual(sent, [{"command": "protocols"}])
        self.assertEqual(lines, ["ADS-L 0.1.0 (ADS-L 4 SRD-860 Issue 2)",
                                 "ALP-TAS 0.2.0 (ALP-TAS 2024 FLARM-generation frame)"])

    def test_a_status_pushed_before_the_answer_is_passed_over(self):
        bravo = {"command": "protocols", "adsb_version": "0.1.0", "adsb_specification": "DF17"}
        self.assertEqual(protocol_lines(PUSHED, bravo)[1], ["ADS-B 0.1.0 (DF17)"])

    def test_firmware_before_the_frame_says_it_does_not_report_them(self):
        unknown = {"command": "acknowledgement", "acknowledge": False, "reason": "unknown_command"}
        with self.assertRaises(SystemExit) as caught:
            protocol_lines(unknown)
        self.assertEqual(str(caught.exception), blip_protocols.TOO_OLD)

    def test_a_link_too_narrow_for_the_answer_says_so(self):
        narrow = {"command": "acknowledgement", "acknowledge": False, "reason": "small_mtu"}
        with self.assertRaises(SystemExit) as caught:
            protocol_lines(narrow)
        self.assertIn(blip.SMALL_MTU, str(caught.exception))

    def test_the_stacks_named_are_the_registry_s(self):
        protocols = frames.frame_named("protocols", frames.REGISTRY)
        stems = [key["name"][:-len("_version")] for key in protocols["keys"]
                 if key["name"].endswith("_version")]
        self.assertEqual(stems, list(blip_protocols.NAMES))


if __name__ == "__main__":
    unittest.main(verbosity=2)
