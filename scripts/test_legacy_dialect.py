#!/usr/bin/env python3
"""The legacy dialect table, and blip.py's link speaking to a unit of either age through it.

    python3 scripts/test_legacy_dialect.py

Run by test_blip.py as well, which is the file CI invokes. No radio: each fake unit reads
only its own dialect's keys, the way the firmware's flat JSON reader does, and answers in
the words its firmware writes.
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
import legacy_dialect  # noqa: E402

SESSION = 1_700_000_000


def pps_slot(index):
    raw = bytearray(records.RECORD_BYTES)
    raw[0] = 4
    raw[records.PAYLOAD_OFFSET:records.PAYLOAD_OFFSET + 4] = index.to_bytes(4, "little")
    return bytes(raw)


DATA = base64.b64encode(pps_slot(0) + pps_slot(1)).decode()


class LegacyUnit:
    """Firmware before #151."""

    def __init__(self):
        self.heard = []
        self.notify = {}

    async def start_notify(self, uuid, callback):
        self.notify[uuid] = callback

    async def write_gatt_char(self, uuid, data, response):
        frame = json.loads(data)
        self.heard.append(frame)
        for reply in self.answer(frame.get("cmd"), frame):
            self.notify[uuid](None, bytearray(json.dumps(reply).encode()))

    def answer(self, command, frame):
        if command == "status":
            return [{"cmd": "status", "flight": "ground", "die_temp_c": 21}]
        if command == "diag":
            return [{"cmd": "diag", "group": "sys", "part": 0, "more": True, "up_s": 42},
                    {"cmd": "diag", "group": "gnss", "part": 1, "more": True, "valid": False},
                    {"cmd": "diag", "group": "power", "part": 2, "more": False, "mv": 3800,
                     "valid": True, "level": "LOW"}]
        if command == "set" and "alarm" in frame:
            return [{"ack": False, "pending": True, "reason": "confirm"}]
        if command == "list" and "index" in frame:
            return [{"cmd": "session", "log": "diagnostics", "index": 0, "of": 1,
                     "session": SESSION, "records": 2, "closed": True, "truncated": False}]
        if command == "list":
            return [{"cmd": "log", "log": "diagnostics", "ack": True, "sessions": 1,
                     "truncated": False}]
        if command == "read":
            return [{"cmd": "chunk", "log": "diagnostics", "session": SESSION, "from": 0, "n": 2,
                     "eof": True, "data": DATA}]
        return [{"ack": False, "reason": "unknown_cmd"}]


class CurrentUnit(LegacyUnit):
    """Firmware since #151."""

    async def write_gatt_char(self, uuid, data, response):
        frame = json.loads(data)
        self.heard.append(frame)
        for reply in self.answer(frame.get("command"), frame):
            self.notify[uuid](None, bytearray(json.dumps(reply).encode()))

    def answer(self, command, frame):
        if command == "status":
            return [{"command": "status", "flight": "ground", "die_temperature_c": 21}]
        if command == "diagnostics":
            return [{"command": "diagnostics", "group": "system", "part": 0, "more": True,
                     "uptime_s": 42},
                    {"command": "diagnostics", "group": "gnss", "part": 1, "more": True,
                     "valid": False},
                    {"command": "diagnostics", "group": "power", "part": 2, "more": False,
                     "cell_mv": 3800, "battery_valid": True, "power_level": "LOW"}]
        if command == "set" and "alarm_enabled" in frame:
            return [{"acknowledge": False, "pending": True, "reason": "confirm"}]
        if command == "list" and "index" in frame:
            return [{"command": "session", "log": "diagnostics", "index": 0, "of": 1,
                     "session": SESSION, "records": 2, "closed": True, "truncated": False}]
        if command == "list":
            return [{"command": "log", "log": "diagnostics", "acknowledge": True, "sessions": 1,
                     "truncated": False}]
        if command == "read":
            return [{"command": "chunk", "log": "diagnostics", "session": SESSION, "from": 0,
                     "records": 2, "ends_session": True, "data": DATA}]
        return [{"acknowledge": False, "reason": "unknown_command"}]


def converse(unit, *bodies):
    """Each body as one config command, and every reply frame, read back as objects."""

    async def run():
        link = blip.Link(unit, 0)
        await link.open(config=True)
        return [[json.loads(text) for _, text in await link.command(body, 1.0)]
                for body in bodies]

    return asyncio.run(run())


def fetched(unit):
    args = argparse.Namespace(log="diagnostics", window=8, timeout=1.0)

    async def run():
        link = blip.Link(unit, 0)
        await link.open(log=True)
        sessions = await blip.list_sessions(link, args)
        out = io.StringIO()
        await blip.fetch_session(link, args, sessions[0], 0, out)
        return [json.loads(line) for line in out.getvalue().splitlines()]

    with contextlib.redirect_stdout(io.StringIO()):
        return asyncio.run(run())


class AnyUnit:
    def test_the_first_frame_carries_both_dialects_because_the_unit_has_not_spoken_yet(self):
        unit = self.unit()
        converse(unit, {"command": "diagnostics"})
        self.assertEqual(unit.heard[0], {"cmd": "diag", "command": "diagnostics"})

    def test_the_dump_reads_in_todays_names(self):
        dump, = converse(self.unit(), {"command": "diagnostics"})
        self.assertEqual([frame["group"] for frame in dump], ["system", "gnss", "power"])
        self.assertEqual(dump[0]["uptime_s"], 42)
        self.assertEqual(dump[1]["valid"], False)
        self.assertEqual((dump[2]["cell_mv"], dump[2]["battery_valid"], dump[2]["power_level"]),
                         (3800, True, "LOW"))
        self.assertTrue(all(frame["command"] == "diagnostics" for frame in dump))

    def test_a_settings_change_after_the_first_reply_reaches_the_unit_in_its_own_words(self):
        unit = self.unit()
        _, (prompt,) = converse(unit, {"command": "status"},
                                {"command": "set", "alarm_enabled": False})
        self.assertEqual(unit.heard[1], self.set_as_heard)
        self.assertEqual(prompt, {"acknowledge": False, "pending": True, "reason": "confirm"})

    def test_a_session_fetches_end_to_end(self):
        lines = fetched(self.unit())
        self.assertEqual([line["index"] for line in lines if line["type"] == "pps"], [0, 1])
        self.assertEqual(lines[0]["records"], 2)


class AgainstALegacyUnit(AnyUnit, unittest.TestCase):
    unit = LegacyUnit
    set_as_heard = {"cmd": "set", "alarm": False}

    def test_a_refusal_reads_in_todays_words(self):
        _, (refusal,) = converse(self.unit(), {"command": "status"}, {"command": "erase"})
        self.assertEqual(refusal, {"acknowledge": False, "reason": "unknown_command"})


class AgainstACurrentUnit(AnyUnit, unittest.TestCase):
    unit = CurrentUnit
    set_as_heard = {"command": "set", "alarm_enabled": False}


def sections():
    return (("link", legacy_dialect.LINK), ("records", legacy_dialect.RECORDS))


class Table(unittest.TestCase):
    def test_the_dumps_valid_is_the_battery_in_the_power_group_and_the_fix_in_the_gnss_group(self):
        power = {"cmd": "diag", "group": "power", "valid": True, "level": "LOW"}
        gnss = {"cmd": "diag", "group": "gnss", "valid": True}
        self.assertEqual(legacy_dialect.current(legacy_dialect.LINK, power),
                         {"command": "diagnostics", "group": "power", "battery_valid": True,
                          "power_level": "LOW"})
        self.assertEqual(legacy_dialect.current(legacy_dialect.LINK, gnss),
                         {"command": "diagnostics", "group": "gnss", "valid": True})
        self.assertEqual(legacy_dialect.legacy(legacy_dialect.LINK,
                                               legacy_dialect.current(legacy_dialect.LINK, power)),
                         power)

    def test_no_current_key_is_a_legacy_key_of_the_same_frame_so_reading_twice_changes_nothing(self):
        for name, section in sections():
            for selected in {value for entry in section["frames"] for value in entry["when"]}:
                keys = dict(section.get("keys", {}))
                for entry in section["frames"]:
                    if selected in entry["when"]:
                        keys.update(entry.get("keys", {}))
                self.assertFalse(set(keys) & set(keys.values()), (name, selected))

    def test_every_legacy_frame_reads_and_writes_back_to_itself(self):
        legacy_select = {"link": "cmd", "records": "type"}
        for name, section in sections():
            values = section.get("values", {}).get(section["select"], {})
            for entry in section["frames"]:
                for selected in entry["when"]:
                    old_selected = next((old for old, new in values.items() if new == selected),
                                        selected)
                    frame = {legacy_select[name]: old_selected}
                    frame.update({key: held[0] for key, held in entry.get("where", {}).items()})
                    frame.update({old: 1 for old in entry.get("keys", {})})
                    for key, spellings in entry.get("values", {}).items():
                        old_key = next((old for old, new in entry.get("keys", {}).items()
                                        if new == key), key)
                        frame[old_key] = next(iter(spellings))
                    read = legacy_dialect.current(section, frame)
                    self.assertEqual(read[section["select"]], selected)
                    self.assertEqual(legacy_dialect.current(section, read), read)
                    self.assertEqual(legacy_dialect.legacy(section, read), frame)

    def test_the_record_decoders_write_only_todays_names(self):
        renamed = {}
        for entry in legacy_dialect.RECORDS["frames"]:
            for type_name in entry["when"]:
                renamed.setdefault(type_name, set()).update(entry.get("keys", {}))
        legacy_types = set(legacy_dialect.RECORDS["values"]["type"])
        decoders = [(name, fields) for name, fields in records.DIAGNOSTICS_TYPES.values()]
        decoders.append(("fix", records.FLIGHT_FIELDS))
        decoders.extend((name, fields) for name, _, fields in records.FLIGHT_EVENTS.values())
        for name, fields in decoders:
            self.assertNotIn(name, legacy_types)
            written = {field for field, _ in fields}
            self.assertFalse(written & renamed.get(name, set()), name)
            self.assertFalse(written & set(legacy_dialect.RECORDS["keys"]), name)

    def test_a_capture_fetched_before_151_reads_in_todays_names(self):
        old = [{"type": "session", "log": "flights", "session": SESSION, "records": 2},
               {"log": "flights", "session": SESSION, "index": 0, "type": "fix",
                "utc": SESSION + 1, "lat_1e7": 1, "sats": 9},
               {"log": "diagnostics", "session": SESSION, "index": 1, "type": "baro",
                "at_s": SESSION, "alt_mm": 5}]
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "old.ndjson"
            path.write_text("".join(json.dumps(line) + "\n" for line in old), encoding="utf-8")
            note, fix, barometer = records.read_ndjson(str(path))
        self.assertEqual(note, old[0])
        self.assertEqual((fix["utc_s"], fix["latitude_1e7"], fix["satellites"]),
                         (SESSION + 1, 1, 9))
        self.assertEqual((barometer["type"], barometer["altitude_mm"]), ("barometer", 5))


if __name__ == "__main__":
    unittest.main(verbosity=2)
