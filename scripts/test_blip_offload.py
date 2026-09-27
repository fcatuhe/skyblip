#!/usr/bin/env python3
"""The log endpoint conversation and the NDJSON a fetch writes it into.

    python3 scripts/test_blip_offload.py

Run by test_blip.py as well, which is the file CI invokes. No radio, no network,
no sleeps: the endpoint below answers a read the way core/comms/log_link.cpp
does, and fails a host that asks for a frame it did not send or sends a command
while a reply of the last one is still queued.
"""
import argparse
import asyncio
import base64
import contextlib
import io
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip  # noqa: E402
import blip_records as records  # noqa: E402

RECORDS_PER_CHUNK = 12
DEVICE_CHUNKS_MAX = 8
PPS_TYPE = 4
INTERVAL_US_AT = records.PAYLOAD_OFFSET


def slot_naming_its_index(index):
    raw = bytearray(records.RECORD_BYTES)
    raw[0] = PPS_TYPE
    raw[4:8] = (1_700_000_000 + index).to_bytes(4, "little")
    raw[INTERVAL_US_AT:INTERVAL_US_AT + 4] = index.to_bytes(4, "little")
    return bytes(raw)


def flight_slot(index):
    raw = bytearray(records.RECORD_BYTES)
    raw[0:2] = index.to_bytes(2, "little")
    raw[22:24] = records.crc16_ccitt(raw[:22]).to_bytes(2, "little")
    return bytes(raw)


END_TYPE = 17


def end_slot(session_records, dropped=0):
    raw = bytearray(records.RECORD_BYTES)
    raw[0] = END_TYPE
    raw[records.PAYLOAD_OFFSET:records.PAYLOAD_OFFSET + 4] = session_records.to_bytes(4, "little")
    raw[records.PAYLOAD_OFFSET + 4:records.PAYLOAD_OFFSET + 8] = dropped.to_bytes(4, "little")
    return bytes(raw)


def slots_closed_with_end(count, dropped=0):
    """A clean stop: count - 1 pps records, then the end marker that closes the session."""
    return [slot_naming_its_index(index) for index in range(count - 1)] + [end_slot(count, dropped)]


def session_reply(count, closed=True, **extra):
    return dict({"cmd": "session", "index": 0, "of": 1, "session": 1_700_000_000,
                 "records": count, "closed": closed}, **extra)


def device_clamp(count):
    """clamped_chunk_count in log_link.cpp: absent or zero is one chunk, past eight is eight."""
    if count is None or count <= 0:
        return 1
    return min(count, DEVICE_CHUNKS_MAX)


class LogEndpoint:
    """A device that is only its log dialect: it plans a window, cuts chunks, marks the last one."""

    def __init__(self, count, log="diagnostics", closed=True, slot=slot_naming_its_index,
                 per_chunk=RECORDS_PER_CHUNK, slots=None, **session_fields):
        self.log = log
        self.session = session_reply(count, closed=closed, log=log, **session_fields)
        self.slots = slots if slots is not None else [slot(index) for index in range(count)]
        self.per_chunk = per_chunk
        self.pending = []
        self.counts_asked = []

    async def write(self, body):
        if self.pending:
            raise AssertionError("a read went out with %d replies of the last one unread"
                                 % len(self.pending))
        self.counts_asked.append(body.get("count"))
        self.pending = self.window_from(body["from"], device_clamp(body.get("count")))

    async def frame(self, timeout):
        if not self.pending:
            raise AssertionError("the host waited for a frame the device never sent")
        return self.pending.pop(0)

    def window_from(self, start, chunks):
        left = max(len(self.slots) - start, 0)
        to_the_end = (left + self.per_chunk - 1) // self.per_chunk
        planned = min(chunks, to_the_end)
        if planned == 0:
            return [self.chunk(start, 0)]
        return [self.chunk(start + nth * self.per_chunk,
                           min(self.per_chunk, len(self.slots) - start - nth * self.per_chunk))
                for nth in range(planned)]

    def chunk(self, start, count):
        blob = b"".join(self.slots[start:start + count])
        return {"cmd": "chunk", "log": self.log, "session": self.session["session"],
                "from": start, "n": count, "eof": start + count >= len(self.slots),
                "data": base64.b64encode(blob).decode()}


class RefusingEndpoint(LogEndpoint):
    async def write(self, body):
        self.pending = [{"cmd": "log", "ack": False, "reason": "in_flight"}]


def fetch(endpoint, out=None, **overrides):
    settings = dict(log=endpoint.log, window=DEVICE_CHUNKS_MAX, timeout=1.0)
    settings.update(overrides)
    args = argparse.Namespace(**settings)
    with contextlib.redirect_stdout(io.StringIO()) as printed:
        asyncio.run(blip.fetch_session(endpoint, args, endpoint.session, 0, out))
    return printed.getvalue()


def fetched_lines(endpoint, **overrides):
    out = io.StringIO()
    fetch(endpoint, out, **overrides)
    return [json.loads(line) for line in out.getvalue().splitlines()]


def of_type(lines, name):
    return [line for line in lines if line.get("type") == name]


class WindowedRead(unittest.TestCase):
    def test_a_window_is_chunks_and_the_default_is_the_eight_the_device_allows(self):
        endpoint = LogEndpoint(100)
        fetch(endpoint)
        self.assertEqual(endpoint.counts_asked, [8, 8])

    def test_a_window_the_device_would_clamp_is_asked_for_already_clamped(self):
        endpoint = LogEndpoint(100)
        fetch(endpoint, window=60)
        self.assertEqual(endpoint.counts_asked, [8, 8])

    def test_no_reply_is_left_queued_once_a_session_has_been_fetched(self):
        endpoint = LogEndpoint(100)
        fetch(endpoint)
        self.assertEqual(endpoint.pending, [])

    def test_a_window_of_zero_asks_for_one_chunk_at_a_time(self):
        endpoint = LogEndpoint(100)
        fetch(endpoint, window=0)
        self.assertEqual(endpoint.counts_asked, [1] * 9)

    def test_a_read_that_runs_past_the_end_stops_on_eof_and_not_on_the_count(self):
        endpoint = LogEndpoint(20)
        fetch(endpoint)
        self.assertEqual(endpoint.counts_asked, [8])

    def test_the_host_clamps_a_count_the_way_log_link_clamps_it(self):
        self.assertEqual([blip.chunks_per_read(n) for n in (-3, 0, 1, 3, 8, 9, 4000)],
                         [1, 1, 1, 3, 8, 8, 8])

    def test_every_record_arrives_once_and_in_the_order_it_was_written(self):
        lines = of_type(fetched_lines(LogEndpoint(100)), "pps")
        self.assertEqual([line["index"] for line in lines], list(range(100)))
        self.assertEqual([line["interval_us"] for line in lines], list(range(100)))

    def test_a_session_of_exactly_one_window_ends_on_the_eof_of_its_eighth_chunk(self):
        endpoint = LogEndpoint(96)
        lines = of_type(fetched_lines(endpoint), "pps")
        self.assertEqual(len(lines), 96)
        self.assertEqual(endpoint.counts_asked, [8])

    def test_a_refusal_in_place_of_a_chunk_stops_the_fetch_with_the_reason_given(self):
        with self.assertRaises(SystemExit) as caught:
            fetch(RefusingEndpoint(24))
        self.assertIn("in_flight", str(caught.exception))


class TornTail(unittest.TestCase):
    def test_the_last_record_of_an_unclosed_diagnostics_session_is_not_written(self):
        lines = of_type(fetched_lines(LogEndpoint(5, closed=False)), "pps")
        self.assertEqual([line["index"] for line in lines], [0, 1, 2, 3])

    def test_the_ndjson_says_which_index_was_dropped_and_why(self):
        note, = of_type(fetched_lines(LogEndpoint(5, closed=False)), "torn")
        self.assertEqual(note["index"], 4)
        self.assertEqual(note["session"], 1_700_000_000)
        self.assertIn("torn write", note["reason"])

    def test_a_closed_session_keeps_its_last_record(self):
        lines = fetched_lines(LogEndpoint(5))
        self.assertEqual([line["index"] for line in of_type(lines, "pps")], [0, 1, 2, 3, 4])
        self.assertEqual(of_type(lines, "torn"), [])

    def test_a_flights_session_keeps_its_tail_because_every_record_carries_a_crc(self):
        lines = fetched_lines(LogEndpoint(5, log="flights", closed=False, slot=flight_slot))
        self.assertEqual(len(of_type(lines, "fix")), 5)
        self.assertEqual(of_type(lines, "torn"), [])

    def test_a_fetch_that_stopped_short_of_the_end_keeps_the_last_record_it_got(self):
        written = []
        sink = records.SessionSink("diagnostics", session_reply(5, closed=False), written.append)
        sink.write({"index": 0, "type": "pps"})
        sink.finish(complete=False)
        self.assertEqual([line["type"] for line in written], ["session", "pps"])
        self.assertIsNone(sink.torn)

    def test_the_dropped_tail_is_counted_as_neither_kept_nor_unreadable(self):
        printed = fetch(LogEndpoint(5, closed=False))
        self.assertIn("4 records kept, 0 unreadable", printed)
        self.assertIn("index 4 dropped", printed)

    def test_the_summary_names_the_dropped_tail(self):
        summary = records.summarise(fetched_lines(LogEndpoint(5, closed=False)))
        self.assertTrue(any("index 4 dropped" in line for line in summary), summary)

    def test_the_summary_counts_only_the_records_and_not_the_notes(self):
        summary = records.summarise(fetched_lines(LogEndpoint(5, closed=False)))
        self.assertIn("4 records", summary)

    def test_a_closed_session_ends_with_the_end_marker_and_keeps_it(self):
        endpoint = LogEndpoint(5, slots=slots_closed_with_end(5, dropped=2))
        lines = fetched_lines(endpoint)
        self.assertEqual([line["type"] for line in of_type(lines, "pps")], ["pps"] * 4)
        end, = of_type(lines, "end")
        self.assertEqual(end["index"], 4)
        self.assertEqual(end["records"], 5)
        self.assertEqual(end["dropped"], 2)
        self.assertEqual(of_type(lines, "torn"), [])

    def test_the_summary_names_the_devices_own_account_from_the_end_marker(self):
        endpoint = LogEndpoint(5, slots=slots_closed_with_end(5, dropped=2))
        summary = records.summarise(fetched_lines(endpoint))
        self.assertTrue(any("5 records written, 2 dropped" in line for line in summary), summary)

    def test_a_dropped_tail_does_not_send_the_next_fetch_back_for_it(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "capture.ndjson"
            out = io.StringIO()
            fetch(LogEndpoint(5, closed=False), out)
            path.write_text(out.getvalue(), encoding="utf-8")
            resume = records.resume_point(str(path))
        session = session_reply(5, closed=False)
        args = argparse.Namespace(session=None, all=True)
        self.assertEqual(blip.sessions_to_fetch([session], args, resume), [])


LINK_TYPE = 12


def link_slot(connection):
    raw = bytearray(records.RECORD_BYTES)
    raw[0] = LINK_TYPE
    raw[records.PAYLOAD_OFFSET:records.PAYLOAD_OFFSET + 2] = connection.to_bytes(2, "little")
    return bytes(raw)


class LinkRecord(unittest.TestCase):
    # the link record's connection id was once named session, and every capture with a connection in it crashed the fetch
    def test_a_capture_holding_a_link_record_keeps_the_store_session_and_the_connection(self):
        endpoint = LogEndpoint(2, slots=[link_slot(3), end_slot(2)])
        link, = of_type(fetched_lines(endpoint), "link")
        self.assertEqual(link["session"], 1_700_000_000)
        self.assertEqual(link["link_session"], 3)


class TruncatedSession(unittest.TestCase):
    def header_of(self, **fields):
        lines = fetched_lines(LogEndpoint(24, **fields))
        return of_type(lines, "session")[0]

    def test_the_header_line_carries_the_truncated_flag_the_device_gave(self):
        header = self.header_of(truncated=True)
        self.assertTrue(header["truncated"])
        self.assertEqual(header["records"], 24)
        self.assertEqual(header["log"], "diagnostics")

    def test_a_session_reply_that_does_not_mention_truncation_is_not_truncated(self):
        self.assertFalse(self.header_of()["truncated"])

    def test_the_summary_says_a_truncated_session_has_no_boot_and_no_config_record(self):
        summary = records.summarise([records.session_note(
            "diagnostics", session_reply(24, truncated=True))])
        self.assertTrue(any("truncated" in line and "no boot" in line for line in summary),
                        summary)

    def test_the_listing_marks_a_truncated_session_that_was_also_cut_short(self):
        text = records.session_text(session_reply(24, closed=False, truncated=True))
        self.assertEqual(text, "session 1700000000  24 records  power cut, truncated")

    def test_a_type_filter_does_not_hide_the_caveats_the_corpus_came_with(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "capture.ndjson"
            out = io.StringIO()
            fetch(LogEndpoint(5, closed=False, truncated=True), out)
            path.write_text(out.getvalue(), encoding="utf-8")
            args = argparse.Namespace(path=str(path), summary=True, type="gnss")
            with contextlib.redirect_stdout(io.StringIO()) as printed:
                blip.run_decode(args)
        self.assertIn("truncated", printed.getvalue())
        self.assertIn("index 4 dropped", printed.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
