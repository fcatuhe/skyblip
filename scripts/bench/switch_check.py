"""Per-switch-kind retune, gap and margin statistics, outliers, keying lateness.

usage: python3 scripts/bench/switch_check.py <capture.ndjson>...
"""
import collections
import statistics
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records as r


def quantile(values, p):
    return values[min(len(values) - 1, int(len(values) * p))]


def report(path):
    recs = [x for x in r.read_ndjson(path) if x.get("type") not in r.NOTE_TYPES]
    t0 = recs[0]["at_s"]
    switches = [x for x in recs if x["type"] == "switch"]
    print("== %s  %d switch records" % (path.split("/")[-1], len(switches)))
    by = collections.defaultdict(list)
    for x in switches:
        by[x["change"]].append(x)
    for change, xs in sorted(by.items()):
        ahead = [x for x in xs if x["armed_ahead"]]
        took = sorted(x["took_us"] for x in xs)
        gap = sorted(x["gap_us"] for x in xs)
        margin = sorted(x["margin_us"] for x in ahead) or [0]
        print("  %-9s n=%5d  took med %5d p99 %5d max %5d | gap med %4d p99 %4d max %5d | "
              "margin(ahead) min %6d p01 %6d med %6d | late %d" % (
                  change, len(xs), statistics.median(took), quantile(took, .99), took[-1],
                  statistics.median(gap), quantile(gap, .99), gap[-1],
                  margin[0], quantile(margin, .01), statistics.median(margin),
                  sum(x["late"] for x in xs)))
    odd = [x for x in switches if x["late"] or x["took_us"] > 3000 or
           (x["armed_ahead"] and x["margin_us"] < 1000)]
    for x in odd[:20]:
        print("   odd rel %d.%03d %s" % (x["at_s"] - t0, x["into_ms"],
              {k: x[k] for k in ("change", "to_hz", "took_us", "gap_us", "margin_us", "armed_ahead", "late")}))
    own = [x for x in recs if x["type"] == "burst" and x["verdict"] == "transmitted"]
    print("  transmitted %d, names %d (compare with diag tx_ok / tx_named; timing.missed should be 0)"
          % (len(own), sum(x["callsign"] for x in own)))
    keyed = sorted(x["tx_keyed_us"] for x in own) or [0]
    print("  tx_keyed_us med %d p99 %d max %d" % (statistics.median(keyed), quantile(keyed, .99), keyed[-1]))


for path in sys.argv[1:]:
    report(path)
