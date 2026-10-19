"""Per-switch-kind retune, gap and margin statistics, outliers, keying lateness.

usage: python3 scripts/bench/switch_check.py <capture.ndjson>...
"""
import collections
import statistics
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records


def quantile(values, fraction):
    return values[min(len(values) - 1, int(len(values) * fraction))]


def report(path):
    records = [record for record in blip_records.read_ndjson(path) if record.get("type") not in blip_records.NOTE_TYPES]
    first_s = records[0]["at_s"]
    switches = [record for record in records if record["type"] == "switch"]
    print("== %s  %d switch records" % (path.split("/")[-1], len(switches)))
    by_change = collections.defaultdict(list)
    for record in switches:
        by_change[record["change"]].append(record)
    for change, changed in sorted(by_change.items()):
        ahead = [record for record in changed if record["armed_ahead"]]
        took = sorted(record["took_us"] for record in changed)
        gap = sorted(record["gap_us"] for record in changed)
        margin = sorted(record["margin_us"] for record in ahead) or [0]
        print("  %-9s n=%5d  took med %5d p99 %5d max %5d | gap med %4d p99 %4d max %5d | "
              "margin(ahead) min %6d p01 %6d med %6d | late %d" % (
                  change, len(changed), statistics.median(took), quantile(took, .99), took[-1],
                  statistics.median(gap), quantile(gap, .99), gap[-1],
                  margin[0], quantile(margin, .01), statistics.median(margin),
                  sum(record["late"] for record in changed)))
    odd = [record for record in switches if record["late"] or record["took_us"] > 3000 or
           (record["armed_ahead"] and record["margin_us"] < 1000)]
    for record in odd[:20]:
        print("   odd rel %d.%03d %s" % (record["at_s"] - first_s, record["into_ms"],
              {key: record[key] for key in ("change", "to_hz", "took_us", "gap_us", "margin_us", "armed_ahead", "late")}))
    own = [record for record in records if record["type"] == "burst" and record["verdict"] == "transmitted"]
    print("  transmitted %d, names %d (compare with diag tx_ok / tx_named; timing.missed should be 0)"
          % (len(own), sum(record["callsign"] for record in own)))
    keyed = sorted(record["tx_keyed_us"] for record in own) or [0]
    print("  tx_keyed_us med %d p99 %d max %d" % (statistics.median(keyed), quantile(keyed, .99), keyed[-1]))


for path in sys.argv[1:]:
    report(path)
