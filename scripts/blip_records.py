"""Decode the 24-byte records a skyBlip offloads (blip_ndjson.py keeps them as NDJSON).

The diagnostics tables are built from blip_diagnostics_layout.py, which
scripts/diagnostics_layout.py generates from schemas/diagnostics_layout.v1.json, as it generates
the tables server/app/javascript/skyblip/diagnostics.js decodes the same records with in the
browser. The flight log tables match firmware/core/flight/log_record.h byte for byte, with the names of
schemas/flight_log.v1.schema.json.
"""
import blip_diagnostics_layout as layout

RECORD_BYTES = 24
PAYLOAD_OFFSET = 8
STORES = ("flights", "diagnostics")


class RecordError(Exception):
    """A slot that is no record: erased flash, a torn write, or an unknown type."""


def uint8(at):
    return lambda raw, flags: raw[at]


def int8(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 1], "little", signed=True)


def uint24(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 3], "little")


def uint16(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 2], "little")


def int16(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 2], "little", signed=True)


def uint32(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 4], "little")


def int32(at):
    return lambda raw, flags: int.from_bytes(raw[at:at + 4], "little", signed=True)


def enum8(at, names):
    # INFO: fc 20sep26 the ordinal is the wire, and flight_enum_fields() reads .names off the closure
    def read(raw, flags):
        return names[raw[at]] if raw[at] < len(names) else raw[at]

    read.names = names
    return read


def flag(bit):
    return lambda raw, flags: bool(flags & (1 << bit))


def bits(at, shift, mask):
    return lambda raw, flags: (raw[at] >> shift) & mask


def valid_only(inner, bit):
    return lambda raw, flags: inner(raw, flags) if flags & (1 << bit) else None


def vouched_flag(recorded, bit):
    return lambda raw, flags: bool(flags & (1 << bit)) if flags & (1 << recorded) else None


def unless_unknown(inner, recorded, valid):
    # INFO: fc 20nov26 older records wrote no vouching bit, so they keep the value

    def read(raw, flags):
        if flags & (1 << recorded) and not flags & (1 << valid):
            return None
        return inner(raw, flags)

    return read


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
    # INFO: fc 20nov26 rounds as core/units/units.h does

    def read(raw, flags):
        centi = div_round(int.from_bytes(raw[at:at + 2], "little") * 36000, 512)
        return div_round(centi % 36000, 100) % 360

    return read


ENUMS = layout.ENUMS
SOURCE = ENUMS["source"]
FLIGHT_STATE = ENUMS["flight_state"]
POWER_LEVEL = ENUMS["power_level"]
CHARGE = ENUMS["charge"]
# INFO: fc 12nov26 comms::BravoState, the link registry's bravo enum with unpaired as its 0
BRAVO = ("unpaired", "waiting", "live", "link_down", "receiver_down", "no_utc", "gps_off",
         "waiting_gps_off", "waiting_utc", "finds_fix", "cell_low")
READERS = {"u8": uint8, "i8": int8, "u16": uint16, "i16": int16, "u24": uint24, "u32": uint32,
           "i32": int32}


def reader(field):
    kind = field["kind"]
    if kind == "flag":
        read = flag(field["bit"])
    elif kind == "enum8":
        read = enum8(field["at"], ENUMS[field["enum"]])
    elif kind == "bits":
        read = bits(field["at"], field["shift"], field["mask"])
    else:
        read = READERS[kind](field["at"])
    if "scale" in field:
        read = scaled(read, field["scale"])
    if "vouch_bit" in field:
        return unless_unknown(read, field["vouch_bit"], field["valid_bit"])
    if "valid_bit" in field:
        return valid_only(read, field["valid_bit"])
    return read


DIAGNOSTICS_TYPES = {code: (name, tuple((field["name"], reader(field)) for field in fields))
                     for code, name, fields in layout.TYPES}

FLIGHT_FIELDS = (
    ("latitude_1e7", int32(2)), ("longitude_1e7", int32(6)), ("altitude_msl_m", int16(10)),
    ("altitude_hae_m", added(int16(10), int8(12))), ("speed_q", uint16(13)),
    ("speed_mm_s", scaled(uint16(13), 250)), ("track_c9", uint16(15)), ("track_deg", track_degrees(15)),
    ("climb_e8", int16(17)), ("climb_mm_s", scaled(int16(17), 125)), ("satellites", uint8(19)),
    ("hdop_e2", scaled(uint8(20), 10)), ("flight_state", flag_field(5, 0x03, FLIGHT_STATE)),
    ("fix_valid", flag(0)), ("utc_valid", flag(1)), ("pps_locked", flag(2)),
    ("climb_valid", flag(3)), ("geoid_measured", flag(4)), ("session_end", flag(7)),
)


# INFO: fc 15oct26 a fix's track is nine bits, so the top bit of byte 16 marks an event instead
FLIGHT_EVENT_MARK = 0x80
FLIGHT_KIND_OFFSET = 16

TARGET_FIELDS = (
    ("address", uint24(2)), ("address_mapping_table", uint8(5)), ("distance_m", uint16(6)),
    ("relative_altitude_m", unless_unknown(int16(8), 2, 3)), ("relative_bearing_deg", uint16(10)),
    ("closing_mps", int16(12)), ("alarm", uint8(14)),
    ("aircraft_category", uint8(15)), ("source", enum8(17, SOURCE)), ("rssi_dbm", int8(18)),
    ("dismissed", flag(0)), ("in_formation", flag(1)), ("relative_altitude_valid", vouched_flag(2, 3)),
)

# INFO: fc 15oct26 kind: (name, the byte its flags are read from, its fields)
FLIGHT_EVENTS = {
    1: ("status", 9, (
        ("cell_mv", uint16(2)), ("percent", uint8(4)), ("power_level", enum8(5, POWER_LEVEL)),
        ("charge", enum8(6, CHARGE)), ("tracked", uint8(7)), ("tracked_peak", uint8(8)),
        ("battery_valid", flag(0)), ("charging", flag(1)), ("external_power", flag(2)))),
    2: ("alarm", 19, TARGET_FIELDS),
    3: ("encounter", 19, TARGET_FIELDS + (("heard_s", uint16(20)),)),
    4: ("bravo", 3, (("bravo", enum8(2, BRAVO)),)),
}


def flight_enum_fields():
    for field, read in FLIGHT_FIELDS:
        names = getattr(read, "names", None)
        if names is not None:
            yield field, names
    for _, _, fields in FLIGHT_EVENTS.values():
        for field, read in fields:
            names = getattr(read, "names", None)
            if names is not None:
                yield field, names


def enum_fields():
    for _, _, fields in layout.TYPES:
        for field in fields:
            if field["kind"] == "enum8":
                yield field["name"], ENUMS[field["enum"]]


def decode_record(store, raw, base_utc_s):
    if store == "diagnostics":
        return decode_diagnostics_record(raw)
    return decode_flight_record(raw, base_utc_s)


def decode_diagnostics_record(raw):
    if len(raw) != RECORD_BYTES:
        raise RecordError("short slot of %d bytes" % len(raw))
    if all(byte == 0xFF for byte in raw):
        raise RecordError("erased")
    if raw[0] not in DIAGNOSTICS_TYPES:
        raise RecordError("unknown type %d" % raw[0])
    name, fields = DIAGNOSTICS_TYPES[raw[0]]
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
        value = read(payload, flags)
        if value is not None:
            decoded[field] = value
    return decoded


def decode_flight_record(raw, base_utc_s):
    if len(raw) != RECORD_BYTES:
        raise RecordError("short slot of %d bytes" % len(raw))
    if all(byte == 0xFF for byte in raw):
        raise RecordError("erased")
    if crc16_ccitt(raw[:22]) != int.from_bytes(raw[22:24], "little"):
        raise RecordError("crc")
    if raw[FLIGHT_KIND_OFFSET] & FLIGHT_EVENT_MARK:
        return decode_flight_event(raw, base_utc_s)
    flags = raw[21]
    decoded = {"type": "fix", "utc_s": base_utc_s + int.from_bytes(raw[0:2], "little")}
    for field, read in FLIGHT_FIELDS:
        decoded[field] = read(raw, flags)
    return decoded


def decode_flight_event(raw, base_utc_s):
    kind = raw[FLIGHT_KIND_OFFSET] & ~FLIGHT_EVENT_MARK & 0xFF
    if kind not in FLIGHT_EVENTS:
        raise RecordError("unknown event kind %d" % kind)
    name, flags_at, fields = FLIGHT_EVENTS[kind]
    flags = raw[flags_at]
    decoded = {"type": name, "utc_s": base_utc_s + int.from_bytes(raw[0:2], "little")}
    for field, read in fields:
        value = read(raw, flags)
        if value is not None:
            decoded[field] = value
    return decoded


def crc16_ccitt(data, crc=0):
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode_chunk(store, raw, base_utc_s, first_index):
    if len(raw) % RECORD_BYTES:
        raise ValueError("chunk of %d bytes is not whole records" % len(raw))
    out = []
    for slot in range(len(raw) // RECORD_BYTES):
        at = slot * RECORD_BYTES
        try:
            decoded = decode_record(store, raw[at:at + RECORD_BYTES], base_utc_s)
        except RecordError as error:
            decoded = {"type": "unreadable", "error": str(error)}
        out.append(dict(index=first_index + slot, **decoded))
    return out
