#!/usr/bin/env python3
"""Read the 1090 MHz frames a skyBlip Bravo refused, as its development image prints them.

    python3 scripts/adsb_rejects.py console.log
    some-serial-tool | python3 scripts/adsb_rejects.py

Each `adsb reject status=crc rssi=-87 chips=<54 hex>` line, as
firmware/products/skyblip_bravo/main.cpp logs it, becomes one line: the DF, the ICAO, the CRC
residue, the doubtful chip pairs, and what would have made the frame pass, if anything. A summary
follows. Other lines are skipped, so a whole console log can be piped in.
firmware/products/skyblip_bravo/README.md, Reading the rejects, says how to read the output.
"""
import collections
import re
import sys

SYNC_CHIPS = 0x95
CHIP_BYTES = 27
BITS = 112
STREAM_CHIPS = 8 + 8 * CHIP_BYTES
GENERATOR = 0x1FFF409
EXTENDED_SQUITTER = 17
SLIPS = (1, 2, -1, -2)

LINE = re.compile(r"adsb reject status=(?P<status>\w+) rssi=(?P<rssi>-?\d+|none) "
                  r"chips=(?P<chips>[0-9a-f]{54})\b")
DROPPED = re.compile(r"--- (?P<count>\d+) messages dropped ---")

Reading = collections.namedtuple(
    "Reading", "status rssi df icao type_code residue doubtful flip shift slip")


def main(argv):
    if len(argv) > 1:
        with open(argv[1], encoding="utf-8", errors="replace") as source:
            readings, dropped = read_console(source)
    else:
        sys.stdin.reconfigure(errors="replace")
        readings, dropped = read_console(sys.stdin)
    for line in summary(readings, dropped):
        print(line)
    return 0


def read_console(lines):
    """Prints each refused frame as it comes, so a live console shows them before it closes."""
    readings = []
    dropped = 0
    for line in lines:
        match = LINE.search(line)
        if match:
            readings.append(read(match))
            print(describe(readings[-1]), flush=True)
        lost = DROPPED.search(line)
        if lost:
            dropped += int(lost["count"])
    return readings, dropped


def read(match):
    stream = stream_of(bytes.fromhex(match["chips"]))
    bits, doubtful = decode(stream)
    value = number(bits)
    residue = remainder(value)
    df = value >> (BITS - 5)
    icao = (value >> (BITS - 32)) & 0xFFFFFF
    type_code = (value >> (BITS - 37)) & 0x1F if df == EXTENDED_SQUITTER else None
    flip = shift = None
    slip = []
    if residue != 0:
        flip = next((bit for bit in range(1, BITS + 1) if SYNDROMES[bit] == residue), None)
    if residue != 0 and flip is None:
        for first, last, by in runs(slips(stream)):
            if first != 8:
                slip.append((first, last, by))
            elif shift is None:
                shift = by
    return Reading(match["status"], match["rssi"], df, icao, type_code, residue, doubtful, flip,
                   shift, slip)


def stream_of(chips):
    stream = [(SYNC_CHIPS >> (7 - i)) & 1 for i in range(8)]
    for byte in chips:
        stream.extend((byte >> (7 - i)) & 1 for i in range(8))
    return stream


def decode(stream):
    """A Mode S 1 is the chips 10 and a 0 is 01, so a bit is the inverse of its second chip."""
    bits = [1 - stream[2 * i + 1] for i in range(BITS)]
    doubtful = [i + 1 for i in range(BITS) if stream[2 * i] == stream[2 * i + 1]]
    return bits, doubtful


def number(bits):
    value = 0
    for bit in bits:
        value = (value << 1) | bit
    return value


def remainder(value):
    for position in range(BITS - 1, 23, -1):
        if value >> position & 1:
            value ^= GENERATOR << (position - 24)
    return value


SYNDROMES = {bit: remainder(1 << (BITS - bit)) for bit in range(1, BITS + 1)}


def slips(stream):
    """Every chip, from 0 at the first sync chip, where one or two chips more or less pass."""
    for at in range(8, STREAM_CHIPS):
        for by in SLIPS:
            for filler in fillers(abs(by)):
                if by > 0:
                    candidate = stream[:at] + stream[at + by:] + filler
                else:
                    candidate = stream[:at] + filler + stream[at:STREAM_CHIPS + by]
                value = number(decode(candidate)[0])
                if remainder(value) == 0 and value >> (BITS - 5) == EXTENDED_SQUITTER:
                    yield at, by
                    break


def runs(found):
    """One slip inside a run of equal chips passes at every chip of the run, so a run is one."""
    merged = []
    for at, by in sorted(found, key=lambda item: (item[1], item[0])):
        if merged and merged[-1][2] == by and merged[-1][1] == at - 1:
            merged[-1][1] = at
        else:
            merged.append([at, at, by])
    return sorted((tuple(run) for run in merged), key=lambda run: run[0])


def fillers(count):
    return [[(n >> i) & 1 for i in range(count)] for n in range(1 << count)]


def describe(reading):
    passes = []
    if reading.flip is not None:
        doubted = " doubtful" if reading.flip in reading.doubtful else ""
        passes.append("flip=%d%s" % (reading.flip, doubted))
    if reading.shift is not None:
        passes.append("shift=%+d" % reading.shift)
    passes.extend("slip=%+d@%s" % (by, first if first == last else "%d-%d" % (first, last))
                  for first, last, by in reading.slip)
    address = (" icao=%06x tc=%d" % (reading.icao, reading.type_code)
               if reading.df == EXTENDED_SQUITTER else "")
    doubtful = " at " + ",".join(map(str, reading.doubtful)) if reading.doubtful else ""
    return "%s rssi=%s df=%d%s residue=%06x doubtful=%d%s%s" % (
        reading.status, reading.rssi, reading.df, address, reading.residue,
        len(reading.doubtful), doubtful, " " + " ".join(passes) if passes else "")


def summary(readings, dropped=0):
    lost = ["the console dropped %d log messages, so some refused frames are missing" % dropped
            ] if dropped else []
    if not readings:
        return lost + [
            "no adsb reject line: is the image a development one, with a host on the console?"]
    statuses = collections.Counter(reading.status for reading in readings)
    formats = collections.Counter(reading.df for reading in readings)
    repeated = collections.Counter(
        reading.icao for reading in readings if reading.df == EXTENDED_SQUITTER)
    flipped = [reading for reading in readings if reading.flip is not None]
    quarters = [0, 0, 0, 0]
    for reading in readings:
        for bit in reading.doubtful:
            quarters[(bit - 1) * 4 // BITS] += 1
    return [
        "frames %d: %s" % (len(readings),
                           ", ".join("%s %d" % item for item in sorted(statuses.items()))),
        "df: %s" % ", ".join("%d=%d" % item for item in sorted(formats.items())),
        "icao heard twice or more as DF17: %s" % (", ".join(
            "%06x (%d)" % item for item in repeated.most_common() if item[1] > 1) or "none"),
        "one flip clears the residue: %d, on a doubtful bit: %d" % (
            len(flipped), sum(reading.flip in reading.doubtful for reading in flipped)),
        "a whole-frame shift passes: %d" % sum(reading.shift is not None for reading in readings),
        "a slip inside the frame passes: %d" % sum(bool(reading.slip) for reading in readings),
        "doubtful pairs by quarter of the frame, bits 1-28 to 85-112: %s"
        % " ".join(map(str, quarters)),
    ] + lost


if __name__ == "__main__":
    sys.exit(main(sys.argv))
