#!/usr/bin/env python3
"""Turn two captures of one link into a path loss and an e.r.p. estimate.

    ./scripts/blip.py --address A fetch --log diagnostics --out a.ndjson
    ./scripts/blip.py --address B fetch --log diagnostics --out b.ndjson
    python3 scripts/link_budget.py a.ndjson b.ndjson --distance 16.7 --height 1.2 \\
        --antenna-a ANT-868-CW-QW-SMA --antenna-b ANT-868-CW-QW-SMA

No meter stands between the two units, so nothing here measures watts. Each
unit's Burst records say when it transmitted and what it heard, at what level,
from whom; its Config record says what the transmitter was asked for. A burst
sent by one and heard by the other is one instant read twice, so the pairs give
a level per direction, and the distance gives the free-space loss it crossed.
What the receiver heard, plus that loss, less its own antenna, is what the
sender radiated. Every term the arithmetic leans on is in TERMS with its source,
and the answer is printed with all of them. scripts/README.md says how to run one.
"""
import argparse
import bisect
import cmath
import collections
import math
import pathlib
import statistics
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import blip_records as records  # noqa: E402
import power_budget  # noqa: E402

LIMIT_ERP_DBM = 14.0
DBI_TO_DBD_DB = 2.15
FEED_LOSS_DB = 0.5

# INFO: fc 26sep26 midway between the M band's two channels, 0.002 dB from either in free space
MBAND_HZ = 868_300_000
SPEED_OF_LIGHT_M_S = 299_792_458

# INFO: fc 26sep26 both ends stamp where their radio raised the burst, so a poll or two apart
PAIR_WINDOW_S = 0.05

# INFO: fc 26sep26 packet_rssi_dbm() truncates the chip's half-dB steps upwards, 0.25 dB on average
RSSI_TRUNCATION_DB = 0.25

Z95 = 1.96

OWN_VERDICTS = ("transmitted", "lost", "held", "unarmed")

Antenna = collections.namedtuple("Antenna", "gain_dbi source")

ANTENNAS = {
    "ANT-868-CW-QW-SMA": Antenna(1.6, "vendor datasheet peak, sx1262.h kAntennaPeakGainDbiCentiDb"),
}

Term = collections.namedtuple("Term", "name db cited source figures")

PATH = "path"
ERP = "erp"

UNMEASURED_GAIN = "a vendor peak, no pattern measured on this board"
UNMEASURED_FEED = "the 0.5 dB sx1262.h allows, never measured"

TERMS = (
    Term("transmit power", 2.0, True, "SX1262 DS 1.2 table 3-9 TXACC", (PATH,)),
    Term("transmit antenna gain", 1.0, False, UNMEASURED_GAIN, (PATH,)),
    Term("transmit feed loss", 0.5, False, UNMEASURED_FEED, (PATH,)),
    Term("RSSI reading", 2.0, False, "the SX1262 datasheet states no RSSI accuracy", (PATH, ERP)),
    Term("receive antenna gain", 1.0, False, UNMEASURED_GAIN, (PATH, ERP)),
    Term("receive feed loss", 0.5, False, UNMEASURED_FEED, (PATH, ERP)),
)


class Side:
    """One unit's capture, as the link reads it."""

    def __init__(self, label, path, antenna):
        self.label = label
        self.path = path
        self.antenna = antenna
        self.runs = power_budget.read(path)
        self.records = [r for run in self.runs for r in run.records if not r.get("error")]

    def configs(self):
        return {(r["addr"], r["tx_power_dbm"], r["pa_rated_dbm"])
                for r in self.records if r["type"] == "config"}

    def bursts(self):
        return [r for r in self.records if r["type"] == "burst" and r["band"] == "M"]


def instant(record):
    """Seconds on the UTC clock both units share, or None when the phase was never measured."""
    if not (record["utc_dated"] and record["phase_valid"]):
        return None
    return record["at_s"] + record["into_ms"] / 1000.0


def transmitter(side):
    """The address and the transmit power a capture names, or None and why not."""
    configs = side.configs()
    if not configs:
        return None, "%s holds no config record, so it cannot say who sent what" % side.path
    if len(configs) > 1:
        return None, "%s names %d transmitters, fetch one session" % (side.path, len(configs))
    addr, power_dbm, pa_rated_dbm = configs.pop()
    if pa_rated_dbm == 0:
        return None, "%s predates the transmitter fields of its config record" % side.path
    return {"addr": addr, "power_dbm": power_dbm, "pa_rated_dbm": pa_rated_dbm,
            "conducted_dbm": min(power_dbm, pa_rated_dbm)}, None


def pair(sender, sender_addr, receiver):
    """Each transmission of the sender with the reception of it at the receiver, if any."""
    sent = [r for r in sender.bursts() if r["verdict"] == "transmitted"]
    heard = [r for r in receiver.bursts()
             if r["verdict"] not in OWN_VERDICTS and r["addr_valid"] and r["rssi_valid"]
             and r["addr"] == sender_addr]
    dated_heard = sorted((instant(r), r) for r in heard if instant(r) is not None)
    at = [moment for moment, _ in dated_heard]
    taken = set()
    levels = []
    undated = 0
    for burst in sent:
        moment = instant(burst)
        if moment is None:
            undated += 1
            continue
        start = bisect.bisect_left(at, moment - PAIR_WINDOW_S)
        for k in range(start, bisect.bisect_right(at, moment + PAIR_WINDOW_S)):
            reception = dated_heard[k][1]
            if k not in taken and reception["channel"] == burst["channel"]:
                taken.add(k)
                levels.append(reception["rssi_dbm"])
                break
    return {"sent": len(sent) - undated, "undated": undated, "levels": levels,
            "unmatched": len(dated_heard) - len(taken),
            "undated_heard": len(heard) - len(dated_heard)}


def free_space_db(distance_m, hz=MBAND_HZ):
    return 20.0 * math.log10(4.0 * math.pi * distance_m * hz / SPEED_OF_LIGHT_M_S)


def ground_band(distance_m, height_m, hz=MBAND_HZ):
    """How far a flat ground can move this path against free space, as (weakest, strongest) dB.

    Two rays at equal heights, the reflected one flipped in phase as grazing incidence
    flips it, with any strength between none and total.
    """
    wavelength = SPEED_OF_LIGHT_M_S / hz
    reflected_m = math.hypot(distance_m, 2.0 * height_m)
    turn = cmath.exp(-2j * math.pi * (reflected_m - distance_m) / wavelength)
    shifts = [20.0 * math.log10(max(abs(1.0 - (step / 100.0) * distance_m / reflected_m * turn),
                                    1e-6))
              for step in range(101)]
    return min(shifts), max(shifts)


def quiet_distance_m(height_m, hz=MBAND_HZ):
    """Where the reflected ray arrives half a wave late: the ground can only strengthen the path."""
    return 4.0 * height_m * height_m * hz / SPEED_OF_LIGHT_M_S


def spread(figure):
    return math.sqrt(sum(term.db ** 2 for term in TERMS if figure in term.figures))


def direction(link, sender_tx, sender_antenna, receiver_antenna, distance_m, height_m):
    """Path loss and e.r.p. for one direction, or None when no burst crossed it."""
    levels = link["levels"]
    if not levels:
        return None
    level = statistics.fmean(levels) - RSSI_TRUNCATION_DB
    sd = statistics.stdev(levels) if len(levels) > 1 else 0.0
    sampling = Z95 * sd / math.sqrt(len(levels))
    free = free_space_db(distance_m)
    weakest, strongest = ground_band(distance_m, height_m)
    eirp_chain = sender_tx["conducted_dbm"] - FEED_LOSS_DB + sender_antenna.gain_dbi
    receive_chain = receiver_antenna.gain_dbi - FEED_LOSS_DB
    path = eirp_chain + receive_chain - level
    erp = level + free - receive_chain - DBI_TO_DBD_DB
    erp_spread = math.hypot(spread(ERP), sampling)
    return {
        "level": level, "median": statistics.median(levels), "sd": sd,
        "low": min(levels), "high": max(levels), "sampling": sampling,
        "free": free, "path": path, "excess": path - free,
        "path_spread": math.hypot(spread(PATH), sampling),
        "weakest": weakest, "strongest": strongest,
        "erp": erp, "erp_chain": eirp_chain - DBI_TO_DBD_DB,
        "erp_low": erp - strongest - erp_spread, "erp_high": erp - weakest + erp_spread,
    }


def gnss_distance(receiver, sender_addr):
    """The median distance the receiver's own traffic picture put the sender at, and how many."""
    metres = [r["dist_m"] for r in receiver.records
              if r["type"] == "traffic" and r["addr"] == sender_addr and r["position_valid"]]
    return (statistics.median(metres), len(metres)) if metres else (None, 0)


def caveats(sides, out):
    out("caveats")
    for side in sides:
        for run in side.runs:
            lines = records.caveat_lines(run.notes) + records.missing_index_lines(run.records)
            lines += ["unreadable at index %d: %s" % (r["index"], r["error"])
                      for r in run.unreadable]
            lines += ["a gap record: %d records the ring had to refuse, over %.1f s"
                      % (g["dropped"], g["span_ms"] / 1000.0) for g in run.gaps]
            for line in lines:
                out("  %s: %s" % (side.label, line))
    cited = sum(1 for term in TERMS if term.cited)
    out("  nothing here was calibrated: %d of %d uncertainty terms cite a datasheet, "
        "%d are estimates" % (cited, len(TERMS), len(TERMS) - cited))


def direction_lines(name, link, sender_tx, result, distance_m, height_m, out):
    out("")
    out(name)
    out("  sent      %d bursts asking for %d dBm through the %+d dBm PA row"
        % (link["sent"], sender_tx["power_dbm"], sender_tx["pa_rated_dbm"]))
    if link["undated"] or link["undated_heard"]:
        out("  undated   %d sent and %d heard carry no PPS-measured instant and cannot pair"
            % (link["undated"], link["undated_heard"]))
    heard = len(link["levels"])
    share = 100.0 * heard / link["sent"] if link["sent"] else 0.0
    out("  heard     %d of them (%.1f%%), and %d receptions matched no transmission"
        % (heard, share, link["unmatched"]))
    if result is None:
        out("  no burst crossed this way: nothing to estimate")
        return
    out("  rssi      %.1f dBm mean, %g median, %.1f sd, %d to %d, the mean +-%.2f at 95%%"
        % (result["level"], result["median"], result["sd"], result["low"], result["high"],
           result["sampling"]))
    out("  path      %.1f dB, %+.1f dB against free space at %g m (%.1f dB), +-%.1f"
        % (result["path"], result["excess"], distance_m, result["free"], result["path_spread"]))
    out("  ground    at %g m up the ground alone can move it %+.1f to %+.1f dB"
        % (height_m, result["weakest"], result["strongest"]))
    out("  e.r.p.    %.1f dBm, %.1f to %.1f, against %g; the chain says %.2f"
        % (result["erp"], result["erp_low"], result["erp_high"], LIMIT_ERP_DBM,
           result["erp_chain"]))


def settings_lines(results, tx, antenna, height_m, out):
    """What the two estimates set in sx1262.h, taking the unit that radiates more."""
    out("")
    out("what it sets")
    ceiling = max(result["erp_high"] for result in results)
    lossless = tx["conducted_dbm"] + antenna.gain_dbi - DBI_TO_DBD_DB
    loss = max(0.0, lossless - ceiling)
    out("  kFeedLossCentiDb  %d   what the chain may claim once the uncertainty is spent, now %d"
        % (round(loss * 100), round(FEED_LOSS_DB * 100)))
    if ceiling <= LIMIT_ERP_DBM:
        out("  kConductedDbm     %d    holds: the higher unit reaches %.1f dBm e.r.p. at most"
            % (tx["conducted_dbm"], ceiling))
    else:
        out("  kConductedDbm     %d    comes down to %d: the higher unit reaches %.1f dBm e.r.p."
            % (tx["conducted_dbm"], math.floor(tx["conducted_dbm"] - (ceiling - LIMIT_ERP_DBM)),
               ceiling))
    txacc = next(term for term in TERMS if term.name == "transmit power")
    out("  a third unit at the top of TXACC would reach %.1f dBm: two units are not the line"
        % (ceiling + txacc.db))
    if min(result["weakest"] for result in results) < 0:
        out("  the ground may have weakened the path and lifted that ceiling: at %g m up, "
            "%g m apart it cannot" % (height_m, round(quiet_distance_m(height_m), 1)))


def report(a, b, distance_m, height_m, out):
    tx_a, why = transmitter(a)
    tx_b, why_b = transmitter(b)
    if why or why_b:
        out(why or why_b)
        return False
    out("link  a 0x%06X %s   b 0x%06X %s" % (tx_a["addr"], a.antenna, tx_b["addr"], b.antenna))
    out("      %g m apart, both %g m up" % (distance_m, height_m))
    for receiver, sender, sender_tx in ((b, a, tx_a), (a, b, tx_b)):
        metres, count = gnss_distance(receiver, sender_tx["addr"])
        if count:
            out("      %s puts %s %g m away, the median of %d traffic records"
                % (receiver.label, sender.label, metres, count))
    out("")
    caveats((a, b), out)

    results = []
    for name, sender, receiver, sender_tx in (("a -> b", a, b, tx_a), ("b -> a", b, a, tx_b)):
        link = pair(sender, sender_tx["addr"], receiver)
        result = direction(link, sender_tx, ANTENNAS[sender.antenna], ANTENNAS[receiver.antenna],
                           distance_m, height_m)
        direction_lines(name, link, sender_tx, result, distance_m, height_m, out)
        if result:
            results.append((result, sender_tx, ANTENNAS[sender.antenna]))
    if not results:
        return True
    worst = max(results, key=lambda entry: entry[0]["erp_high"])
    settings_lines([result for result, _, _ in results], worst[1], worst[2], height_m, out)
    return True


def metres(text):
    value = float(text)
    if not value > 0:
        raise argparse.ArgumentTypeError("a distance is more than zero metres, not %s" % text)
    return value


def antenna(text):
    if text not in ANTENNAS:
        raise argparse.ArgumentTypeError("no gain on file for %s: add it to ANTENNAS with its "
                                         "source (%s)" % (text, ", ".join(sorted(ANTENNAS))))
    return text


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("capture_a", help="NDJSON from blip.py fetch --log diagnostics, unit a")
    parser.add_argument("capture_b", help="the same from unit b, over the same stretch")
    parser.add_argument("--distance", type=metres, required=True,
                        help="antenna to antenna, in metres, off a tape")
    parser.add_argument("--height", type=metres, required=True,
                        help="both antennas above the ground, in metres")
    parser.add_argument("--antenna-a", type=antenna, required=True, help="fitted on unit a")
    parser.add_argument("--antenna-b", type=antenna, required=True, help="fitted on unit b")
    args = parser.parse_args(argv)

    a = Side("a", args.capture_a, args.antenna_a)
    b = Side("b", args.capture_b, args.antenna_b)
    return 0 if report(a, b, args.distance, args.height, print) else 1


if __name__ == "__main__":
    sys.exit(main())
