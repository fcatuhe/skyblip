"""Decode the 24-byte records a skyBlip offloads (blip_ndjson.py keeps them as NDJSON).

The tables match firmware/core/diagnostics/payload.h and firmware/core/flight/log_record.h
byte for byte, with the names of schemas/diagnostics_log.v1.schema.json and
schemas/flight_log.v1.schema.json. server/app/javascript/skyblip/diagnostics.js decodes the
diagnostics half again in the browser, and its test holds the two to the same NDJSON, so
change both together.
"""
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
    # INFO: fc 20sep26 the ordinal is the wire, and enum_fields() reads .names back off the closure
    def read(raw, flags):
        return names[raw[at]] if raw[at] < len(names) else raw[at]

    read.names = names
    return read


def vouched_enum8(at, bit, names):

    def read(raw, flags):
        if not flags & (1 << bit):
            return None
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


RESET = ("UNKNOWN", "POWER ON", "RESET PIN", "BROWNOUT", "SOFT RESET", "WATCHDOG", "CPU LOCKUP",
         "CHARGER WAKE", "BUTTON WAKE", "DEBUGGER")
IMAGE_STATE = ("confirmed", "probation", "reverted", "refused")
PROFILE = ("full", "power_run", "flight_run")
ALARM_SETTING = ("off", "visual", "soft", "loud")
REJECT = ("NONE", "NO SOLUTION", "NO RMC", "NO GGA", "STALE", "NO DATE", "JUMP")
STAGE = ("silent", "blind", "solving", "fixed")
VERDICT = ("transmitted", "lost", "held", "unarmed", "received", "named", "bad_crc", "unframed",
           "miskeyed", "undecoded", "unsupported", "unattempted", "declared", "deferred",
           "forced", "busy")
SOURCE = ("adsl_direct", "adsl_uplink", "alptas", "own", "adsb")
BAND = ("M", "O")
SLOT_STATE = ("uplink_rx_o_band", "switch_o_band_to_m_band", "slot0", "hop", "slot1",
              "switch_m_band_to_o_band")
REFUSAL = ("none", "over_budget", "unarmed", "gate_closed", "off_schedule", "held_for_fix")
FLIGHT_STATE = ("unknown", "on_ground", "airborne")
POWER_LEVEL = ("unknown", "normal", "critical", "flat", "low")
# INFO: fc 12nov26 comms::BravoState, the link registry's bravo enum with unpaired as its 0
BRAVO = ("unpaired", "waiting", "live", "link_down", "receiver_down", "no_utc")
CHARGE = ("unknown", "ok", "too_cold", "too_hot")
CONTACT = ("button", "pad")
LINK_ACTION = ("up", "down", "claim_taken", "claim_released", "received", "sent", "dropped")
MTU_EXCHANGE_STARTER = ("none", "central", "unit", "both")
ENDPOINT = ("config", "nmea", "log", "peer")
PLACEMENT = ("idle", "hold", "place", "forced")
WRITE_KIND = ("settings", "flight_record")
CHANGE = ("retune", "hop", "to_o_band", "to_m_band", "wake")
SERVICE = ("config", "ownship", "power", "radio", "traffic", "alarm", "nmea", "flight_log",
           "capture", "screen", "board_poll", "bravo", "adsb", "button", "peer")

DIAGNOSTICS_TYPES = {
    1: ("boot", (
        ("capabilities", uint32(0)), ("firmware_build", uint32(4)), ("firmware_revision", uint16(8)),
        ("firmware_major", uint8(10)), ("firmware_minor", uint8(11)), ("reset", enum8(12, RESET)),
        ("image_state", enum8(13, IMAGE_STATE)), ("profile", vouched_enum8(14, 2, PROFILE)))),
    2: ("config", (
        ("address", uint32(0)), ("battery_offset_mv", int16(4)), ("frequency_trim_e1_ppm", int16(6)),
        ("aircraft_type", uint8(8)), ("address_mapping_table", uint8(9)), ("alarm_volume", uint8(10)),
        ("settings_version", uint8(11)), ("tx_power_dbm", int8(12)), ("pa_rated_dbm", int8(13)),
        ("alarm_setting", vouched_enum8(14, 5, ALARM_SETTING)), ("alarm_enabled", flag(2)),
        ("metric", flag(3)), ("battery_trim_manual", flag(4)))),
    3: ("gnss", (
        ("solution_phase_ms", uint16(0)), ("residual_m", uint16(2)), ("hdop_e2", uint16(4)), ("vdop_e2", uint16(6)),
        ("stage_s", uint16(8)), ("satellites", uint8(10)), ("satellites_in_view", uint8(11)), ("fix_mode", uint8(12)),
        ("reject", enum8(13, REJECT)), ("stage", enum8(14, STAGE)), ("fix_valid", flag(2)),
        ("residual_valid", flag(3)), ("pps_locked", flag(4)), ("geoid_measured", flag(5)),
        ("tx_settled", flag(6)), ("levels_fresh", flag(7)))),
    4: ("pps", (
        ("interval_us", uint32(0)), ("error_us", int32(4)), ("samples", uint32(8)),
        ("holdover_events", uint16(12)), ("since_edge_ms", uint16(14)), ("locked", flag(2)),
        ("utc_valid", flag(3)))),
    5: ("burst", (
        ("address", uint32(0)), ("tx_keyed_us", uint16(4)), ("tx_span_us", uint16(6)),
        ("verdict", enum8(8, VERDICT)), ("source", enum8(9, SOURCE)), ("band", enum8(10, BAND)),
        ("channel", uint8(11)), ("bytes", uint8(12)), ("rssi_dbm", int8(13)), ("key_offset_s", int8(14)),
        ("tx_stage_margin_us", scaled(int8(15), 10)), ("address_valid", flag(2)), ("rssi_valid", flag(3)), ("airborne", flag(4)),
        ("tx_span_valid", flag(5)), ("callsign", flag(6)), ("status", flag(7)))),
    6: ("dwell", (
        ("frequency_hz", uint32(0)), ("start_ms", uint16(4)), ("end_ms", uint16(6)), ("phase_ms", uint16(8)),
        ("duty_permille", uint16(10)), ("state", enum8(12, SLOT_STATE)), ("band", enum8(13, BAND)),
        ("refusal", enum8(14, REFUSAL)), ("noise_dbm", int8(15)), ("tx_allowed", flag(2)),
        ("own_tx_dwell", flag(3)), ("listen_only", flag(4)), ("armed", flag(5)),
        ("burst_armed", flag(6)))),
    7: ("flight", (
        ("speed_mm_s", int32(0)), ("climb_mm_s", int32(4)), ("altitude_msl_m", int16(8)),
        ("hdop_e2", uint16(10)), ("vdop_e2", uint16(12)), ("declared", enum8(14, FLIGHT_STATE)),
        ("confirmed", enum8(15, FLIGHT_STATE)), ("fix_valid", flag(2)), ("rolling", flag(3)),
        ("climb_valid", flag(4)), ("tx_settled", flag(5)))),
    8: ("power", (
        ("cell_mv", uint16(0)), ("supply_warnings", uint16(2)), ("implausible", uint16(4)),
        ("charge_warnings", uint16(6)), ("die_dc", int16(8)), ("percent", uint8(10)),
        ("power_level", enum8(11, POWER_LEVEL)), ("charge", enum8(12, CHARGE)),
        ("trim_offset_mv", int16(13)), ("sample_offset_mv", int8(15)), ("charging", flag(2)), ("external_power", flag(3)),
        ("battery_valid", flag(4)), ("die_valid", flag(5)), ("caution", flag(6)),
        ("trim_learned", flag(7)))),
    9: ("barometer", (
        ("pressure_mpa", uint32(0)), ("altitude_mm", int32(4)), ("climb_mm_s", int32(8)),
        ("temperature_dc", int16(12)), ("active", flag(2)), ("temperature_valid", flag(3)),
        ("climb_adopted", flag(4)))),
    10: ("motion", (
        ("slip_mg", int16(0)), ("normal_mg", int16(2)), ("lateral_mg", int16(4)),
        ("longitudinal_mg", int16(6)), ("most_normal_mg", int16(8)), ("least_normal_mg", int16(10)),
        ("imu_error", uint8(12)), ("sensor_error", uint8(13)), ("slip_valid", flag(2)),
        ("gload_valid", flag(3)), ("fitted", flag(4)))),
    11: ("contact", (
        ("at_ms", uint32(0)), ("held_ms", uint32(4)), ("contact", enum8(8, CONTACT)),
        ("gesture", uint8(9)), ("down", flag(2)))),
    12: ("link", (
        ("link_session", uint16(0)), ("payload_bytes", uint16(2)), ("frame_bytes", uint16(4)),
        ("holder", uint16(6)), ("drops", uint16(8)), ("action", enum8(10, LINK_ACTION)),
        ("endpoint", enum8(11, ENDPOINT)),
        ("mtu_exchange_started_by", enum8(12, MTU_EXCHANGE_STARTER)), ("claim_held", flag(2)))),
    13: ("traffic", (
        ("address", uint32(0)), ("distance_m", uint16(4)), ("bearing_deg", uint16(6)),
        ("relative_altitude_m", unless_unknown(int16(8), 6, 7)),
        ("closing_mps", int16(10)), ("alarm", uint8(12)), ("source", enum8(13, SOURCE)),
        ("rssi_dbm", int8(14)), ("tracked", uint8(15)), ("assessed", flag(2)), ("dismissed", flag(3)),
        ("in_formation", flag(4)), ("position_valid", flag(5)),
        ("relative_altitude_valid", vouched_flag(6, 7)))),
    14: ("write", (
        ("waited_ms", uint32(0)), ("phase_ms", uint16(4)), ("requests", uint16(6)), ("writes", uint16(8)),
        ("forced", uint16(10)), ("placement", enum8(12, PLACEMENT)), ("kind", enum8(13, WRITE_KIND)),
        ("pending", flag(2)))),
    15: ("screen", (
        ("since_page_shown_ms", uint32(0)), ("page", uint8(4)), ("mode", uint8(5)), ("prompt", uint8(6)),
        ("alarm", uint8(7)), ("render_us", uint32(8)), ("present_us", uint32(12)), ("backlight", flag(2)),
        ("powered", flag(3)), ("holding", flag(4)))),
    16: ("gap", (
        ("dropped", uint32(0)), ("span_ms", uint32(4)), ("total", uint32(8)), ("capacity", uint16(12)))),
    17: ("end", (
        ("records", uint32(0)), ("dropped", uint32(4)))),
    # INFO: fc 21sep26 duty counters wrap at 65536 on purpose: a reader subtracts, never clamps
    18: ("duty", (
        ("panel_partial_refreshes", uint16(0)), ("panel_full_refreshes", uint16(2)),
        ("backlight_ms", uint16(4)), ("rx_armed_ms", uint16(6)), ("tx_keyed_ms", uint16(8)),
        ("ble_connected_ms", uint16(10)), ("annunciator_ms", uint16(12)))),
    19: ("switch", (
        ("to_hz", uint32(0)), ("margin_us", int32(4)), ("took_us", uint16(8)), ("gap_us", uint16(10)),
        ("change", enum8(12, CHANGE)), ("armed_ahead", flag(2)), ("late", flag(3)))),
    20: ("loop", (
        ("passes", uint16(0)), ("busy_ms", uint16(2)), ("worst_pass_us", uint32(4)),
        ("worst_pass_phase_ms", uint16(8)), ("worst_gap_ms", uint16(10)), ("worst_gap_phase_ms", uint16(12)),
        ("worst_service", enum8(14, SERVICE)), ("worst_tick_ms", uint8(15)))),
    21: ("adsb", (
        ("address", uint24(0)), ("adsb_version", valid_only(bits(3, 0, 0x07), 2)),
        ("gva", valid_only(bits(3, 3, 0x03), 3)), ("pressure_altitude_ft", valid_only(int32(4), 4)),
        ("height_difference_ft", valid_only(int16(8), 5)),
        ("height_difference_age_ms", valid_only(int16(10), 5)),
        ("gnss_relative_altitude_m", valid_only(int16(12), 6)),
        ("pressure_relative_altitude_m", valid_only(int16(14), 7)), ("adsb_version_valid", flag(2)),
        ("gva_valid", flag(3)), ("pressure_altitude_valid", flag(4)), ("height_difference_valid", flag(5)),
        ("gnss_relative_altitude_valid", flag(6)), ("pressure_relative_altitude_valid", flag(7)))),
}

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
    for _, fields in DIAGNOSTICS_TYPES.values():
        for field, read in fields:
            names = getattr(read, "names", None)
            if names is not None:
                yield field, names


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
