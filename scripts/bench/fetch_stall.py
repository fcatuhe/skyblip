"""usage: python3 scripts/bench/fetch_stall.py <capture.ndjson>...

Fetch window in 10 s buckets: late dwells, nav_ms, gap drops, and how long the loop went quiet after each log command."""
import pathlib, sys, collections, statistics as st
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records as r
def t(x): return x["at_s"] * 1000 + x["into_ms"]
late = lambda d: d["start_ms"] in (205, 400, 800) and 1 < d["phase_ms"] - d["start_ms"] < 600 and not d["burst_armed"] and not (d["start_ms"] == 205 and d["phase_ms"] >= 700)
for f in sys.argv[1:]:
    recs = [x for x in r.read_ndjson(f) if x.get("type") not in r.NOTE_TYPES]
    rx = [i for i, x in enumerate(recs) if x["type"] == "link" and x["action"] == "received" and x["endpoint"] == "log"]
    if not rx:
        print("== %s: no log commands" % f); continue
    a = recs[rx[0]]["at_s"]; b = recs[rx[-1]]["at_s"]
    quiet = collections.defaultdict(list)
    for i in rx:
        t0 = t(recs[i])
        nxt = next((t(y) for y in recs[i + 1:i + 80] if y["type"] not in ("switch", "pps") and t(y) > t0 + 1), None)
        if nxt: quiet[(recs[i]["at_s"] - a) // 10].append(nxt - t0)
    b_late = collections.Counter(); b_nav = collections.defaultdict(list); b_drop = collections.Counter()
    for x in recs:
        if not (a <= x["at_s"] <= b + 2): continue
        k = (x["at_s"] - a) // 10
        if x["type"] == "dwell" and late(x): b_late[k] += 1
        if x["type"] == "gnss": b_nav[k].append(x["nav_ms"])
        if x["type"] == "gap": b_drop[k] += x["dropped"]
    print("== %s: %d log commands over %d s" % (f.split("/")[-1], len(rx), b - a))
    print("   t_s  cmds quiet_med quiet_max late nav_med nav_max dropped")
    for k in sorted(set(quiet) | set(b_nav)):
        q = quiet.get(k, []); n = b_nav.get(k, [])
        print("  %4d  %4d  %8s  %8s  %4d  %6s  %6s  %6d" % (k * 10, len(q), int(st.median(q)) if q else "-", max(q) if q else "-", b_late[k], int(st.median(n)) if n else "-", max(n) if n else "-", b_drop[k]))
