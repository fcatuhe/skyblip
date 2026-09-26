"""Decode the 24-byte records a skyBlip offloads, and read the NDJSON they are kept in.

Byte-exact against firmware/core/diag/record.h with firmware/core/diag/payload.h
(the diagnostics capture) and firmware/core/flight/log_record.h (the flights
log). Field names and enum spellings follow schemas/diagnostics_log.v1.schema.json.

Importable on its own: an analysis script wants these tables without the BLE
transport in blip.py.
"""
import base64
import datetime
import json

RECORD_BYTES = 24
PAYLOAD_OFFSET = 8
STORES = ("flights", "diagnostics")


class RecordError(Exception):
    """A slot that is no record: erased flash, a torn write, or an unknown type."""


def u8(at):
    return lambda raw, flags: raw[at]


def i8(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 1], "little", signed=True)


def u16(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 2], "little")


def i16(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 2], "little", signed=True)


def u32(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 4], "little")


def i32(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 4], "little", signed=True)


def enum8(at, names):
    # INFO: fc 20sep26 the ordinal is the wire, and enum_fields() reads .names back off the closure
    def read(raw, flags):
        return names[raw[at]] if raw[at] < len(names) else raw[at]

    read.names = names
    return read


def flag(bit):
    return lambda raw, flags: bool(flags & (1 << bit))


def flag_field(shift, mask, names):
    def read(raw, flags):
        value = (flags >> shift) & mask
        return names[value] if value < len(names) else value

    read.names = names
    return read


def scaled(inner, factor):
    return lambda raw, flags: inner(raw, flags) * factor


def added(first, second):
    return lambda raw, flags: first(raw, flags) + second(raw, flags)


def div_round(numerator, denominator):
    return (numerator + denominator // 2) // denominator


def track_degrees(at):
    """Cordic9 back to whole degrees, the way core/units/units.h rounds it."""

    def read(raw, flags):
        centi = div_round(int.from_bytes(raw[at:at + 2], "little") * 36000, 512)
        return div_round(centi % 36000, 100) % 360

    return read


RESET = ("UNKNOWN", "POWER ON", "RESET PIN", "BROWNOUT", "SOFT RESET", "WATCHDOG", "CPU LOCKUP",
         "CHARGER WAKE", "BUTTON WAKE", "DEBUGGER")
IMAGE_STATE = ("confirmed", "probation", "reverted")
REJECT = ("NONE", "NO SOLUTION", "NO RMC", "NO GGA", "STALE", "NO DATE", "JUMP")
STAGE = ("silent", "blind", "solving", "fixed")
VERDICT = ("transmitted", "lost", "held", "unarmed", "received", "named", "bad_crc", "unframed",
           "miskeyed", "undecoded", "unsupported", "unattempted")
SOURCE = ("adsl_direct", "adsl_uplink", "alptas", "own")
BAND = ("M", "O")
SLOT_STATE = ("uplink_rx_o", "switch_o_to_m", "slot0", "hop", "slot1", "switch_m_to_o")
REFUSAL = ("none", "over_budget", "unarmed", "unsettled", "off_schedule")
FLIGHT_STATE = ("unknown", "on_ground", "airborne")
POWER_LEVEL = ("unknown", "normal", "low", "cutoff")
CHARGE = ("unknown", "ok", "too_cold", "too_hot")
CONTACT = ("button", "pad")
LINK_ACTION = ("up", "down", "claim_taken", "claim_released", "received", "sent", "dropped")
ENDPOINT = ("config", "nmea", "log")
PLACEMENT = ("idle", "hold", "place", "forced")
WRITE_KIND = ("settings", "flight_record")

DIAG_TYPES = {
    1: ("boot", (
        ("capabilities", u32(0)), ("fw_build", u32(4)), ("fw_revision", u16(8)),
        ("fw_major", u8(10)), ("fw_minor", u8(11)), ("reset", enum8(12, RESET)),
        ("image_state", enum8(13, IMAGE_STATE)))),
    2: ("config", (
        ("addr", u32(0)), ("battery_offset_mv", i16(4)), ("freq_trim_e1_ppm", i16(6)),
        ("aircraft_type", u8(8)), ("addr_table", u8(9)), ("alarm_volume", u8(10)),
        ("settings_version", u8(11)), ("alarm_enabled", flag(2)), ("metric", flag(3)),
        ("battery_trim_manual", flag(4)))),
    3: ("gnss", (
        ("nav_ms", u16(0)), ("resid_m", u16(2)), ("hdop_e2", u16(4)), ("vdop_e2", u16(6)),
        ("stage_s", u16(8)), ("sats", u8(10)), ("sats_in_view", u8(11)), ("fix_mode", u8(12)),
        ("reject", enum8(13, REJECT)), ("stage", enum8(14, STAGE)), ("fix_valid", flag(2)),
        ("resid_valid", flag(3)), ("pps_locked", flag(4)), ("geoid_measured", flag(5)),
        ("tx_settled", flag(6)))),
    4: ("pps", (
        ("interval_us", u32(0)), ("error_us", i32(4)), ("samples", u32(8)),
        ("holdover_events", u16(12)), ("since_edge_ms", u16(14)), ("locked", flag(2)),
        ("utc_valid", flag(3)))),
    5: ("burst", (
        ("addr", u32(0)), ("tx_keyed_us", u16(4)), ("tx_span_us", u16(6)),
        ("verdict", enum8(8, VERDICT)), ("source", enum8(9, SOURCE)), ("band", enum8(10, BAND)),
        ("channel", u8(11)), ("len", u8(12)), ("rssi_dbm", i8(13)), ("key_offset_s", i8(14)),
        ("addr_valid", flag(2)), ("rssi_valid", flag(3)), ("airborne", flag(4)),
        ("tx_span_valid", flag(5)), ("callsign", flag(6)))),
    6: ("dwell", (
        ("freq_hz", u32(0)), ("start_ms", u16(4)), ("end_ms", u16(6)), ("phase_ms", u16(8)),
        ("duty_permille", u16(10)), ("state", enum8(12, SLOT_STATE)), ("band", enum8(13, BAND)),
        ("refusal", enum8(14, REFUSAL)), ("noise_dbm", i8(15)), ("tx_allowed", flag(2)),
        ("own_tx_dwell", flag(3)), ("listen_only", flag(4)), ("armed", flag(5)),
        ("burst_armed", flag(6)))),
    7: ("flight", (
        ("speed_mm_s", i32(0)), ("climb_mm_s", i32(4)), ("alt_msl_m", i16(8)),
        ("hdop_e2", u16(10)), ("vdop_e2", u16(12)), ("declared", enum8(14, FLIGHT_STATE)),
        ("confirmed", enum8(15, FLIGHT_STATE)), ("fix_valid", flag(2)), ("rolling", flag(3)),
        ("climb_valid", flag(4)), ("tx_settled", flag(5)))),
    8: ("power", (
        ("cell_mv", u16(0)), ("supply_warnings", u16(2)), ("implausible", u16(4)),
        ("charge_warnings", u16(6)), ("die_dc", i16(8)), ("percent", u8(10)),
        ("level", enum8(11, POWER_LEVEL)), ("charge", enum8(12, CHARGE)),
        ("trim_offset_mv", i16(13)), ("charging", flag(2)), ("external_power", flag(3)),
        ("valid", flag(4)), ("die_valid", flag(5)), ("caution", flag(6)),
        ("trim_learned", flag(7)))),
    9: ("baro", (
        ("pressure_mpa", u32(0)), ("alt_mm", i32(4)), ("climb_mm_s", i32(8)),
        ("temperature_dc", i16(12)), ("active", flag(2)), ("temperature_valid", flag(3)),
        ("climb_adopted", flag(4)))),
    10: ("motion", (
        ("slip_mg", i16(0)), ("normal_mg", i16(2)), ("lateral_mg", i16(4)),
        ("longitudinal_mg", i16(6)), ("most_normal_mg", i16(8)), ("least_normal_mg", i16(10)),
        ("imu_error", u8(12)), ("sensor_error", u8(13)), ("slip_valid", flag(2)),
        ("gload_valid", flag(3)), ("fitted", flag(4)))),
    11: ("contact", (
        ("at_ms", u32(0)), ("held_ms", u32(4)), ("contact", enum8(8, CONTACT)),
        ("gesture", u8(9)), ("down", flag(2)))),
    12: ("link", (
        ("session", u16(0)), ("payload_bytes", u16(2)), ("frame_bytes", u16(4)),
        ("holder", u16(6)), ("drops", u16(8)), ("action", enum8(10, LINK_ACTION)),
        ("endpoint", enum8(11, ENDPOINT)), ("claim_held", flag(2)))),
    13: ("traffic", (
        ("addr", u32(0)), ("dist_m", u16(4)), ("bearing_deg", u16(6)), ("rel_alt_m", i16(8)),
        ("closing_mps", i16(10)), ("alarm", u8(12)), ("source", enum8(13, SOURCE)),
        ("rssi_dbm", i8(14)), ("tracked", u8(15)), ("assessed", flag(2)), ("dismissed", flag(3)),
        ("in_formation", flag(4)), ("position_valid", flag(5)))),
    14: ("write", (
        ("waited_ms", u32(0)), ("phase_ms", u16(4)), ("requests", u16(6)), ("writes", u16(8)),
        ("forced", u16(10)), ("placement", enum8(12, PLACEMENT)), ("kind", enum8(13, WRITE_KIND)),
        ("pending", flag(2)))),
    15: ("screen", (
        ("since_ms", u32(0)), ("page", u8(4)), ("mode", u8(5)), ("prompt", u8(6)),
        ("alarm", u8(7)), ("backlight", flag(2)), ("powered", flag(3)), ("holding", flag(4)))),
    16: ("gap", (
        ("dropped", u32(0)), ("span_ms", u32(4)), ("total", u32(8)), ("capacity", u16(12)))),
    17: ("end", (
        ("records", u32(0)), ("dropped", u32(4)))),
    # INFO: fc 21sep26 duty counters wrap at 65536 on purpose: a reader subtracts, never clamps
    18: ("duty", (
        ("panel_partial_refreshes", u16(0)), ("panel_full_refreshes", u16(2)),
        ("backlight_ms", u16(4)), ("rx_armed_ms", u16(6)), ("tx_keyed_ms", u16(8)),
        ("ble_connected_ms", u16(10)), ("annunciator_ms", u16(12)))),
}

FLIGHT_FIELDS = (
    ("lat_1e7", i32(2)), ("lon_1e7", i32(6)), ("alt_msl_m", i16(10)),
    ("alt_hae_m", added(i16(10), i8(12))), ("speed_q", u16(13)),
    ("speed_mm_s", scaled(u16(13), 250)), ("track_c9", u16(15)), ("track_deg", track_degrees(15)),
    ("climb_e8", i16(17)), ("climb_mm_s", scaled(i16(17), 125)), ("sats", u8(19)),
    ("hdop_e2", scaled(u8(20), 10)), ("flight_state", flag_field(5, 0x03, FLIGHT_STATE)),
    ("fix_valid", flag(0)), ("utc_valid", flag(1)), ("pps_locked", flag(2)),
    ("climb_valid", flag(3)), ("geoid_measured", flag(4)), ("session_end", flag(7)),
)


def enum_fields():
    """Every diagnostics field an ordinal is read through, as (field, names) pairs."""
    for _, fields in DIAG_TYPES.values():
        for field, read in fields:
            names = getattr(read, "names", None)
            if names is not None:
                yield field, names


def decode_record(store, raw, base_utc):
    if store == "diagnostics":
        return decode_diag_record(raw)
    return decode_flight_record(raw, base_utc)


def decode_diag_record(raw):
    if len(raw) != RECORD_BYTES:
        raise RecordError("short slot of %d bytes" % len(raw))
    if all(byte == 0xFF for byte in raw):
        raise RecordError("erased")
    if raw[0] not in DIAG_TYPES:
        raise RecordError("unknown type %d" % raw[0])
    name, fields = DIAG_TYPES[raw[0]]
    flags = raw[1]
    payload = raw[PAYLOAD_OFFSET:]
    decoded = {
        "type": name,
        "at_s": int.from_bytes(raw[4:8], "little"),
        "into_ms": int.from_bytes(raw[2:4], "little"),
        "phase_valid": bool(flags & 0x01),
        "utc_dated": bool(flags & 0x02),
    }
    for field, read in fields:
        decoded[field] = read(payload, flags)
    return decoded


def decode_flight_record(raw, base_utc):
    if len(raw) != RECORD_BYTES:
        raise RecordError("short slot of %d bytes" % len(raw))
    if all(byte == 0xFF for byte in raw):
        raise RecordError("erased")
    if crc16_ccitt(raw[:22]) != int.from_bytes(raw[22:24], "little"):
        raise RecordError("crc")
    flags = raw[21]
    decoded = {"type": "fix", "utc": base_utc + int.from_bytes(raw[0:2], "little")}
    for field, read in FLIGHT_FIELDS:
        decoded[field] = read(raw, flags)
    return decoded


def crc16_ccitt(data, crc=0):
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode_chunk(store, encoded, base_utc, first_index):
    """One chunk's base64 into decoded records, each stamped with its own index."""
    raw = base64.b64decode(encoded, validate=True)
    if len(raw) % RECORD_BYTES:
        raise ValueError("chunk of %d bytes is not whole records" % len(raw))
    out = []
    for slot in range(len(raw) // RECORD_BYTES):
        at = slot * RECORD_BYTES
        try:
            decoded = decode_record(store, raw[at:at + RECORD_BYTES], base_utc)
        except RecordError as error:
            decoded = {"type": "unreadable", "error": str(error)}
        out.append(dict(index=first_index + slot, **decoded))
    return out


NOTE_TYPES = ("session", "torn")
TORN_TAIL_REASON = "the last record of a session the device did not close may be a torn write"


def is_note(line):
    """A line about a session rather than a record out of one."""
    return line.get("type") in NOTE_TYPES


def session_text(session):
    marks = ["closed" if session.get("closed") else "power cut"]
    if session.get("truncated"):
        marks.append("truncated")
    return "session %d  %d records  %s" % (session["session"], session["records"],
                                           ", ".join(marks))


def session_note(log, session):
    return {"type": "session", "log": log, "session": session["session"],
            "records": session["records"], "closed": bool(session.get("closed")),
            "truncated": bool(session.get("truncated"))}


def tail_cannot_be_vouched_for(log, session):
    # INFO: fc 20sep26 a diagnostics record carries no CRC, so a power cut leaves a plausible slot
    return log == "diagnostics" and not session.get("closed", True)


class SessionSink:
    """One session as NDJSON: what the device said of it, its records, and a tail nobody vouches for."""

    def __init__(self, log, session, emit):
        self.log = log
        self.session = session["session"]
        self.emit = emit
        self.drops_tail = tail_cannot_be_vouched_for(log, session)
        self.kept = 0
        self.unreadable = 0
        self.torn = None
        self.held = None
        emit(session_note(log, session))

    def write(self, record):
        self.release()
        self.held = dict(log=self.log, session=self.session, **record)

    def finish(self, complete):
        if self.held is None or not (complete and self.drops_tail):
            self.release()
            return
        self.torn, self.held = self.held, None
        self.emit({"type": "torn", "log": self.log, "session": self.session,
                   "index": self.torn["index"], "reason": TORN_TAIL_REASON})

    def release(self):
        if self.held is None:
            return
        if self.held.get("error"):
            self.unreadable += 1
        else:
            self.kept += 1
        self.emit(self.held)
        self.held = None


def read_ndjson(path):
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if line.strip():
                yield json.loads(line)


def resume_point(path):
    """The last record a previous fetch kept, as (session, index), or None."""
    last = None
    try:
        for record in read_ndjson(path):
            if "session" in record and "index" in record:
                last = (record["session"], record["index"])
    except (OSError, json.JSONDecodeError):
        return last
    return last


def stamp_of(record):
    """into_ms reaches 1200 on a slot 1 burst, so it is printed beside the second, not inside it."""
    if "utc" in record:
        return utc_text(record["utc"])
    second = utc_text(record["at_s"]) if record.get("utc_dated") else "+%ds" % record.get("at_s", 0)
    return "%s %dms" % (second, record.get("into_ms", 0))


def utc_text(seconds):
    moment = datetime.datetime.fromtimestamp(seconds, datetime.timezone.utc)
    return moment.strftime("%Y-%m-%dT%H:%M:%S")


ENVELOPE = ("index", "session", "log", "type", "at_s", "into_ms", "utc")


def render(record):
    body = " ".join("%s=%s" % (key, value) for key, value in record.items()
                    if key not in ENVELOPE)
    stamp = "" if is_note(record) else stamp_of(record)
    return "%6s %-28s %-10s %s" % (record.get("index", ""), stamp,
                                   record.get("type", "?"), body)


def summarise(lines_in):
    """Counts per type, the span, and every hole: an invisible hole teaches a wrong number."""
    counts = {}
    stamped = []
    gaps = []
    unreadable = []
    end = None
    records = [line for line in lines_in if not is_note(line)]
    for record in records:
        counts[record.get("type", "?")] = counts.get(record.get("type", "?"), 0) + 1
        moment = record.get("utc", record.get("at_s"))
        if moment is not None:
            stamped.append((moment, record))
        if record.get("type") == "gap":
            gaps.append(record)
        if record.get("type") == "end":
            end = record
        if record.get("error"):
            unreadable.append(record)

    lines = caveat_lines([line for line in lines_in if is_note(line)])
    lines.append("%d records" % sum(counts.values()))
    if stamped:
        first = min(stamped, key=lambda pair: pair[0])
        last = max(stamped, key=lambda pair: pair[0])
        lines.append("span %s to %s, %d s" % (stamp_of(first[1]), stamp_of(last[1]),
                                              last[0] - first[0]))
    for kind in sorted(counts):
        lines.append("  %-12s %d" % (kind, counts[kind]))
    lines.extend(missing_index_lines(records))
    for gap in gaps:
        lines.append("gap at %s: %s records dropped over %s ms, %s since arming, ring holds %s" %
                     (stamp_of(gap), gap.get("dropped"), gap.get("span_ms"), gap.get("total"),
                      gap.get("capacity")))
    for bad in unreadable:
        lines.append("unreadable at index %s: %s" % (bad.get("index"), bad.get("error")))
    if not gaps and not unreadable:
        lines.append("no gap records and no unreadable slots")
    if end is not None:
        lines.append("device reports %s records written, %s dropped" %
                     (end.get("records"), end.get("dropped")))
    return lines


def caveat_lines(notes):
    """What an analysis must not assume about these records, before it is handed any count."""
    out = []
    for note in notes:
        for line in caveats_of(note):
            if line not in out:
                out.append(line)
    return out


def caveats_of(note):
    session = note["session"]
    if note["type"] == "torn":
        return ["session %s: index %s dropped, %s" % (session, note.get("index"),
                                                      note.get("reason", TORN_TAIL_REASON))]
    out = []
    if note.get("truncated"):
        out.append("session %s: truncated, its first sector was evicted, so it carries no boot "
                   "and no config record" % session)
    if not note.get("closed"):
        out.append("session %s: not closed, the device stopped without ending it" % session)
    return out


def missing_index_lines(records):
    seen = sorted(record["index"] for record in records if "index" in record)
    holes = [(a, b) for a, b in zip(seen, seen[1:]) if b > a + 1]
    return ["missing indices %d..%d" % (a + 1, b - 1) for a, b in holes]
