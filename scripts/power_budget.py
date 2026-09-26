#!/usr/bin/env python3
"""Turn a power run into a power budget: what the cell spent, and on what.

    ./scripts/blip.py fetch --log diagnostics --all --out run.ndjson
    python3 scripts/power_budget.py run.ndjson --pack-mah 2400

No current is sensed anywhere on the board (firmware/core/power/README.md), so
nothing here measures milliamps. Two halves meet instead. The Duty records say
how long each consumer was on, the table below says what one costs, and the
Power records say how far the cell actually fell. The difference between the
modelled draw and the measured one is the table's error, and printing it is the
point of the tool: no current in the table was metered on this board, and the
rows marked (est) do not even have a datasheet figure behind them.
scripts/README.md says how to run one.
"""
import argparse
import collections
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip_records as records  # noqa: E402

WRAP = 1 << 16

ELAPSED = ("elapsed", None, 0)

Consumer = collections.namedtuple("Consumer", "name milliamps counter cited source")


def on_ms(field):
    return ("ms", field, 0)


def events(field, each_ms):
    return ("count", field, each_ms)


CONSUMERS = (
    Consumer("GNSS receiver", 29.0, ELAPSED, True,
             "L76K hardware design V1.0 table 2, tracking"),
    Consumer("nRF52840, flash, rails", 3.5, ELAPSED, False,
             "10 ms service pass, UARTE at 115200, no CONFIG_PM"),
    Consumer("IMU hub", 0.6, ELAPSED, False,
             "BHI260AP accelerometer only, the gyroscope is never read"),
    Consumer("barometer, lamp, divider", 0.3, ELAPSED, False,
             "BME280 forced mode at 1 Hz"),
    Consumer("868 MHz receive", 4.8, on_ms("rx_armed_ms"), True,
             "SX1262 DS 1.2 table 3-5, Rx boosted FSK on the DC-DC"),
    Consumer("radio TCXO", 2.0, on_ms("rx_armed_ms"), False,
             "DS 1.2 excludes the TCXO from every figure it gives"),
    Consumer("868 MHz transmit", 90.0, on_ms("tx_keyed_ms"), True,
             "DS 1.2 table 3-6, +14 dBm through the +22 dBm PA config sx1262.h writes"),
    Consumer("panel partial refresh", 8.0, events("panel_partial_refreshes", 460), False,
             "460 ms is GxEPD2's D67 partial settle, ssd1681.h"),
    Consumer("panel full refresh", 8.0, events("panel_full_refreshes", 2500), False,
             "2.5 s full settle, ssd1681.h and shutdown.h kParkMs"),
    Consumer("backlight", 20.0, on_ms("backlight_ms"), False, "an LED nobody has metered"),
    Consumer("Bluetooth connected", 1.5, on_ms("ble_connected_ms"), False,
             "three connections at a 498-byte MTU"),
    Consumer("buzzer and haptic", 40.0, on_ms("annunciator_ms"), False,
             "a motor and a magnet nobody has metered"),
)

# INFO: fc 21sep26 the callsign burst is not gated on airborne, so parked keys 58 ms a minute
AIRBORNE_KEYED_MS_PER_MINUTE = 160

FULL_PERCENT = 99


class Run:
    """One session out of a capture, as the budget reads it."""

    def __init__(self, session, closed=True, truncated=False):
        self.session = session
        self.closed = closed
        self.truncated = truncated
        self.records = []

    @property
    def duty(self):
        return [r for r in self.records if r["type"] == "duty"]

    @property
    def power(self):
        return [r for r in self.records if r["type"] == "power" and r["valid"]]

    @property
    def gaps(self):
        return [r for r in self.records if r["type"] == "gap"]


def read(path):
    """Every diagnostics session in an NDJSON capture, in the order fetched."""
    runs = {}
    order = []
    for line in records.read_ndjson(path):
        if line.get("log") not in (None, "diagnostics"):
            continue
        if line.get("store") not in (None, "diagnostics"):
            continue
        number = line.get("session")
        if number is None:
            continue
        if number not in runs:
            runs[number] = Run(number)
            order.append(number)
        run = runs[number]
        if line.get("type") == "session":
            run.closed = bool(line.get("closed"))
            run.truncated = bool(line.get("truncated"))
        elif "index" in line and line.get("type") in ("duty", "power", "gap", "end"):
            run.records.append(line)
    for run in runs.values():
        run.records.sort(key=lambda r: r["index"])
    return [runs[number] for number in order]


def stamp(record):
    """Seconds, UTC when a fix dated the record and since boot when not."""
    phase = record["into_ms"] / 1000.0 if record.get("phase_valid") else 0.0
    return record["at_s"] + phase


def delta(before, after, field):
    """A counter's movement, across the wrap the firmware chose over saturation."""
    return (after[field] - before[field]) % WRAP


def refused(run, before, after):
    """Why this pair of Duty records cannot be subtracted, or None."""
    if any(before["index"] < gap["index"] < after["index"] for gap in run.gaps):
        return "the ring refused records inside the interval"
    if before.get("utc_dated") != after.get("utc_dated"):
        return "the clock became UTC-dated inside the interval"
    if stamp(after) <= stamp(before):
        return "the interval does not move forwards"
    return None


def interval_charge(before, after, seconds):
    """Milliamp-seconds per consumer over one interval."""
    charge = {}
    for consumer in CONSUMERS:
        kind, field, each_ms = consumer.counter
        if kind == "elapsed":
            on_seconds = seconds
        elif kind == "ms":
            on_seconds = delta(before, after, field) / 1000.0
        else:
            on_seconds = delta(before, after, field) * each_ms / 1000.0
        charge[consumer.name] = consumer.milliamps * on_seconds
    return charge


def posture(before, after, seconds):
    """Airborne or parked, read off the air the transmitter spent."""
    keyed_per_minute = delta(before, after, "tx_keyed_ms") * 60.0 / seconds
    return "airborne" if keyed_per_minute >= AIRBORNE_KEYED_MS_PER_MINUTE else "parked"


def model(run):
    """The modelled draw over every interval the corpus can vouch for."""
    charge = {consumer.name: 0.0 for consumer in CONSUMERS}
    held = {"airborne": 0.0, "parked": 0.0}
    seconds = 0.0
    skipped = []
    duty = run.duty
    for before, after in zip(duty, duty[1:]):
        reason = refused(run, before, after)
        if reason:
            skipped.append(reason)
            continue
        span = stamp(after) - stamp(before)
        for name, spent in interval_charge(before, after, span).items():
            charge[name] += spent
        held[posture(before, after, span)] += span
        seconds += span
    return charge, seconds, held, skipped


def measure(run, pack_mah):
    """What the cell says it spent, which needs the pack's capacity from outside."""
    power = run.power
    if len(power) < 2 or pack_mah is None:
        return None
    first, last = power[0], power[-1]
    hours = (stamp(last) - stamp(first)) / 3600.0
    if hours <= 0:
        return None
    whole = first["percent"] >= FULL_PERCENT and last["level"] == "cutoff"
    points = first["percent"] - last["percent"]
    mah = pack_mah if whole else pack_mah * points / 100.0
    return {
        "hours": hours,
        "mah": mah,
        "milliamps": mah / hours,
        "whole": whole,
        "points": points,
        "from_mv": first["cell_mv"],
        "to_mv": last["cell_mv"],
        "from_level": first["level"],
        "to_level": last["level"],
    }


def clock(seconds):
    return "%dh %02dm" % (int(seconds) // 3600, int(seconds) % 3600 // 60)


def caveats(run, skipped, out):
    out("caveats")
    if not run.closed:
        out("  the device did not close this session: it stopped where the power did")
    if run.truncated:
        out("  truncated: the sector this run opened in was recycled, the start is gone")
    for gap in run.gaps:
        out("  a gap record: %d records the ring had to refuse, over %.1f s"
            % (gap["dropped"], gap["span_ms"] / 1000.0))
    for reason in dict.fromkeys(skipped):
        out("  %d intervals dropped: %s" % (skipped.count(reason), reason))
    cited = sum(1 for consumer in CONSUMERS if consumer.cited)
    out("  no current in the table was metered on this board: %d of %d cite a datasheet, "
        "%d are estimates" % (cited, len(CONSUMERS), len(CONSUMERS) - cited))


def report(run, pack_mah, out):
    charge, seconds, held, skipped = model(run)
    cell = measure(run, pack_mah)

    out("run %d, %d records" % (run.session, len(run.records)))
    out("")
    caveats(run, skipped, out)
    if not seconds:
        out("")
        out("no interval this corpus can vouch for: nothing to budget")
        return
    out("")

    if cell:
        out("span      %s   %d -> %d mV   %s -> %s"
            % (clock(seconds), cell["from_mv"], cell["to_mv"],
               cell["from_level"], cell["to_level"]))
    else:
        out("span      %s" % clock(seconds))
    out("posture   airborne %s, parked %s" % (clock(held["airborne"]), clock(held["parked"])))
    out("")

    modelled_mas = sum(charge.values())
    out("%-26s %8s %7s %6s  %s" % ("consumer", "mAh", "mA", "share", "milliamps from"))
    for consumer in CONSUMERS:
        mas = charge[consumer.name]
        if mas <= 0:
            continue
        out("%-26s %8.1f %7.2f %5.0f%%  %s%s"
            % (consumer.name, mas / 3600.0, mas / seconds, 100.0 * mas / modelled_mas,
               "" if consumer.cited else "(est) ", consumer.source))
    out("%-26s %8.1f %7.2f %5.0f%%" % ("modelled", modelled_mas / 3600.0,
                                       modelled_mas / seconds, 100.0))

    if not cell:
        out("")
        out("no measured draw: pass --pack-mah, and a run with two believable readings")
        return
    out("%-26s %8.1f %7.2f         %s"
        % ("measured", cell["mah"], cell["milliamps"],
           "%g mAh pack, full to cutoff" % pack_mah if cell["whole"]
           else "%g mAh pack, %d points of the gauge's own curve" % (pack_mah, cell["points"])))
    modelled_ma = modelled_mas / seconds
    out("%-26s %8s %+7.2f         the table explains %.0f%% of what the cell lost"
        % ("residual", "", modelled_ma - cell["milliamps"],
           100.0 * modelled_ma / cell["milliamps"]))
    if not cell["whole"]:
        out("")
        out("that measured figure leans on the two curves in core/power/battery.cpp, a")
        out("textbook cell and not this pack: only a run from full to cutoff measures the")
        out("capacity instead of assuming it")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture", help="NDJSON from blip.py fetch --log diagnostics")
    parser.add_argument("--pack-mah", type=float, default=None,
                        help="the cell's rated capacity, which the device cannot know")
    parser.add_argument("--session", type=int, default=None, help="one session, not every one")
    args = parser.parse_args(argv)

    runs = [run for run in read(args.capture)
            if args.session is None or run.session == args.session]
    if not runs:
        print("no diagnostics session in %s" % args.capture)
        return 1
    for at, run in enumerate(runs):
        if at:
            print("")
        report(run, args.pack_mah, print)
    return 0


if __name__ == "__main__":
    sys.exit(main())
