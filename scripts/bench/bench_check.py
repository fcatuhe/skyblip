"""Per-build bench checks: keying, burst ends, dwell ends, reception, PPS, GNSS, noise, reinits.

usage: python3 scripts/bench/bench_check.py <E68BD9.ndjson> <0B1B2C.ndjson> [more pairs...]
Files are read one by one; the cross-match pairs every file with every other file.
"""
import collections
import statistics
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records as r

M = 0xFFFFFFFF
SLOT = {0: (450, 790), 1: (800, 990)}
SLOT0_LAST_FROM_BUILD = {844: 787, 856: 789, 863: 790, 874: 791}
SLOT1_LAST_FROM_BUILD = {856: 992, 863: 992, 874: 993}


def mix(x):
    x ^= x >> 16
    x = (x * 0x7FEB352D) & M
    x ^= x >> 15
    x = (x * 0x846CA68B) & M
    x ^= x >> 16
    return x


def q(values, p):
    return values[min(len(values) - 1, int(len(values) * p))] if values else None


def stats(values):
    v = sorted(values)
    if not v:
        return "n=0"
    return "n=%d min %d med %d p99 %d max %d" % (len(v), v[0], statistics.median(v), q(v, .99), v[-1])


def load(path):
    raw = list(r.read_ndjson(path))
    recs = [x for x in raw if x.get("type") not in r.NOTE_TYPES]
    return recs


def windows_for(build):
    slots = dict(SLOT)
    for since, last in sorted(SLOT0_LAST_FROM_BUILD.items()):
        if build >= since:
            slots[0] = (450, last)
    for since, last in sorted(SLOT1_LAST_FROM_BUILD.items()):
        if build >= since:
            slots[1] = (800, last)
    return slots


def integrity(recs):
    by_session = collections.defaultdict(list)
    for x in recs:
        by_session[x["session"]].append(x["index"])
    out = []
    for session, idx in by_session.items():
        c = collections.Counter(idx)
        dups = [i for i, n in c.items() if n > 1]
        s = sorted(c)
        holes = [(a, b) for a, b in zip(s, s[1:]) if b != a + 1]
        out.append("session %d: %d records, index %d..%d, dups %d %s, holes %d %s" % (
            session, len(idx), s[0], s[-1], len(dups), dups[:5], len(holes), holes[:5]))
    return out


def report(path, recs):
    boot = [x for x in recs if x["type"] == "boot"]
    build = boot[0]["fw_build"] if boot else 0
    addr = next(x["addr"] for x in recs if x["type"] == "config")
    ground = mix(addr) % 10
    slots = windows_for(build)
    first_s, last_s = recs[0]["at_s"], max(x["at_s"] for x in recs)
    print("== %s  addr %06X  build %s  boots %d  ground %d  span %d s" % (
        path.split("/")[-1], addr, [b["fw_build"] for b in boot], len(boot), ground, last_s - first_s))
    for line in integrity(recs):
        print("  " + line)
    types = collections.Counter(x["type"] for x in recs)
    for t in ("gap", "unreadable", "end"):
        if types[t]:
            print("  !! %d %s records" % (types[t], t))

    # keying
    own = [x for x in recs if x["type"] == "burst" and x["verdict"] == "transmitted"]
    print("  verdicts own", dict(collections.Counter(x["verdict"] for x in recs
                                                    if x["type"] == "burst" and x["verdict"] != "received"
                                                    and x["verdict"] != "named" and x["verdict"] != "unframed")))
    for kind, sel in (("pos", lambda b: not b["callsign"]), ("name", lambda b: b["callsign"])):
        bs = [b for b in own if sel(b)]
        if not bs:
            continue
        print("  %-4s keyed_us %s" % (kind, stats([b["tx_keyed_us"] for b in bs])))
        print("  %-4s SetTx->TxDone_us %s" % (kind, stats([b["tx_span_us"] - b["tx_keyed_us"] for b in bs])))
        print("  %-4s span_us %s" % (kind, stats([b["tx_span_us"] for b in bs])))

    # burst ends
    for slot in (0, 1):
        lo, hi = slots[slot]
        inst, done, logged = [], [], []
        for b in own:
            if b["callsign"]:
                continue
            s = b["at_s"]
            if s % 10 != ground or (s // 10) & 1 != slot:
                continue
            instant = lo + mix(addr ^ mix(s)) % (hi - lo + 1)
            inst.append(instant)
            done.append(instant + (b["tx_span_us"] + 999) // 1000)
            logged.append(b["into_ms"])
        if inst:
            print("  slot %d window %d..%d: instant max %d, instant+span max %d, record into_ms max %d (n=%d)"
                  % (slot, lo, hi, max(inst), max(done), max(logged), len(inst)))
    names = [b for b in own if b["callsign"]]
    if names:
        into = [1000 + b["into_ms"] for b in names]
        key = [1000 + b["into_ms"] - (b["tx_span_us"] + 999) // 1000 for b in names]
        print("  name: record into_ms(+1000) max %d, record-span (deadline lower bound) max %d, n=%d"
              % (max(into), max(key), len(names)))
    bad = [b for b in own if not b["callsign"] and b["at_s"] % 10 != ground]
    if bad:
        print("  !! %d position bursts off the ground second" % len(bad))

    # dwell ends
    dw = [x for x in recs if x["type"] == "dwell"]
    ends = collections.defaultdict(collections.Counter)
    for d in dw:
        ends[d["state"]][(d["start_ms"], d["end_ms"])] += 1
    for state, c in sorted(ends.items()):
        print("  dwell %-14s (start,end) %s" % (state, dict(c.most_common(4))))
    noise = collections.defaultdict(list)
    for d in dw:
        noise[d["band"]].append(d["noise_dbm"])
    for band, v in sorted(noise.items()):
        nz = [n for n in v if n != 0]
        print("  noise %s: %d dwells, %d sampled, %s" % (band, len(v), len(nz), stats(nz)))
    print("  refusals", dict(collections.Counter(d["refusal"] for d in dw)))

    # reinits and late switches
    sw = [x for x in recs if x["type"] == "switch"]
    re = [x for x in sw if x["took_us"] > 3000 or x["late"]]
    print("  reinit/late switches %d: %s" % (len(re), collections.Counter(
        (x["change"], x["into_ms"], x["late"]) for x in re).most_common(6)))

    # PPS
    pps = [x for x in recs if x["type"] == "pps"]
    if pps:
        hold = [x["holdover_events"] for x in pps]
        print("  pps %d: holdover_events %d..%d, since_edge 65535: %d, unlocked %d, since_edge==1000: %d, "
              "stamped >=990ms: %d, error_us %s" % (
                  len(pps), min(hold), max(hold), sum(x["since_edge_ms"] == 65535 for x in pps),
                  sum(not x["locked"] for x in pps), sum(x["since_edge_ms"] == 1000 for x in pps),
                  sum(x["into_ms"] >= 990 for x in pps), stats([x["error_us"] for x in pps])))

    # GNSS
    g = [x for x in recs if x["type"] == "gnss"]
    if g:
        lost = [x for x in g if not x["fix_valid"]]
        flips = sum(a["fix_valid"] and not b["fix_valid"] for a, b in zip(g, g[1:]))
        secs = sorted({x["at_s"] for x in g})
        miss = [(a - first_s, b - a) for a, b in zip(secs, secs[1:]) if b - a > 1]
        print("  gnss %d: fix invalid %d (losses %d), rejects %s, missing seconds %s" % (
            len(g), len(lost), flips, dict(collections.Counter(x["reject"] for x in g)), miss[:10]))
        print("  gnss sats_in_view %s" % stats([x["sats_in_view"] for x in g]))
    pages = collections.Counter(x["page"] for x in recs if x["type"] == "screen")
    print("  screen pages (records) %s" % dict(pages))
    return addr, own


def cross(name_a, recs_a, addr_a, own_a, name_b, recs_b):
    rx = [x for x in recs_b if x["type"] == "burst" and x["verdict"] in ("received", "named")
          and x.get("addr") == addr_a]
    lo = max(recs_a[0]["at_s"], recs_b[0]["at_s"]) + 1
    hi = min(max(x["at_s"] for x in recs_a), max(x["at_s"] for x in recs_b)) - 1
    t_rx = sorted(x["at_s"] + x["into_ms"] / 1000 for x in rx)
    import bisect
    hit = collections.Counter()
    total = collections.Counter()
    missed = []
    for b in own_a:
        if not lo <= b["at_s"] <= hi:
            continue
        kind = "name" if b["callsign"] else "pos"
        total[kind] += 1
        t = b["at_s"] + b["into_ms"] / 1000
        i = bisect.bisect_left(t_rx, t - 0.030)
        if i < len(t_rx) and t_rx[i] <= t + 0.030:
            hit[kind] += 1
        else:
            missed.append((b["at_s"] - lo, b["into_ms"], kind))
    lens = collections.Counter(x["len"] for x in rx)
    rssi = [x["rssi_dbm"] for x in rx]
    unframed = sum(1 for x in recs_b if x["type"] == "burst" and x["verdict"] == "unframed"
                   and lo <= x["at_s"] <= hi)
    print("  %s -> %s over %d s: %s of %s heard, rx len %s, rssi %s, unframed at rx %d, missed %s" % (
        name_a, name_b, hi - lo, dict(hit), dict(total), dict(lens), stats(rssi), unframed, missed[:10]))


def main():
    paths = sys.argv[1:]
    loaded = []
    for p in paths:
        recs = load(p)
        addr, own = report(p, recs)
        loaded.append((p.split("/")[-1], recs, addr, own))
    print("== reception cross-match (within 30 ms)")
    for a in loaded:
        for b in loaded:
            if a is not b:
                cross(a[0], a[1], a[2], a[3], b[0], b[1])


if __name__ == "__main__":
    main()
