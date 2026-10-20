#!/usr/bin/env python3
"""Self-check for bench/mtu_check.py: one line per ended connection, and the totals option B is weighed on.

    python3 scripts/test_mtu_check.py

Stdlib alone. The records are handcrafted link slots put through the same decoder blip.py fetch uses,
so a field that moves in firmware/core/diagnostics/payload.h fails here rather than reading as zero.
"""
import pathlib
import struct
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "bench"))

import blip_records as records  # noqa: E402
import mtu_check  # noqa: E402

LINK = 12
UP, DOWN, RECEIVED = 0, 1, 4
REFUSED, EXCHANGED, NOT_ASKED = 4, 2, 0
CENTRAL = 1 << 3
UNIT = 1 << 4


def link_record(action, flags=0, payload_bytes=20, settled_ms=0, too_wide=0, request=NOT_ASKED,
                error=0, link_session=1, at_s=100):
    payload = struct.pack("<3H2BH2BH2B", link_session, payload_bytes, 0, link_session, error, 0,
                          action, 0, settled_ms, too_wide, request)
    raw = bytes([LINK, flags]) + struct.pack("<H", 0) + struct.pack("<I", at_s) + payload
    record = records.decode_diagnostics_record(raw)
    record["session"] = 7
    return record


class MtuCheckTest(unittest.TestCase):
    def test_a_connection_line_names_who_exchanged_the_payload_the_delay_and_the_refusals(self):
        line = mtu_check.connection_line(link_record(DOWN, CENTRAL, 182, 240, 0))
        self.assertRegex(line, r"^s7\s+link 1 .* central\s+not_asked\s+182\s+240 ms\s+0$")

    def test_a_refused_request_shows_its_error_and_a_link_nobody_exchanged_shows_none(self):
        line = mtu_check.connection_line(link_record(DOWN, 0, 20, 0, 3, REFUSED, 0x0E))
        self.assertIn("none", line)
        self.assertIn("refused 0x0E", line)
        self.assertRegex(line, r"\s20\s+0 ms\s+3$")

    def test_both_flags_are_two_requests_that_crossed(self):
        self.assertEqual(mtu_check.negotiated_by(link_record(DOWN, CENTRAL | UNIT, 244)), "both")
        self.assertEqual(mtu_check.negotiated_by(link_record(DOWN, UNIT, 244, request=EXCHANGED)), "unit")

    # Only a down record is a whole connection: an up is written again each time the payload moves.
    def test_only_down_records_are_connections_and_the_totals_count_them(self):
        corpus = [
            link_record(UP), link_record(RECEIVED),
            link_record(DOWN, CENTRAL, 182, 300, link_session=1),
            link_record(DOWN, UNIT, 244, 150, request=EXCHANGED, link_session=2),
            link_record(DOWN, 0, 20, 0, 2, REFUSED, 0x06, link_session=3),
        ]
        lines = mtu_check.report_lines(corpus)
        self.assertEqual(len(lines), 5)
        self.assertEqual(lines[-1],
                         "3 connections: 1 exchanged by the central, 1 by the unit, 0 by both, 1 by neither; "
                         "1 unit requests failed; 1 ended at 20 bytes; 2 replies refused small_mtu on 1 connections")

    def test_a_capture_without_down_records_says_so_rather_than_printing_zeros(self):
        self.assertEqual(len(mtu_check.report_lines([link_record(UP)])), 1)
        self.assertIn("no link down records", mtu_check.report_lines([])[0])


if __name__ == "__main__":
    unittest.main(verbosity=2)
