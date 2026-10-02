"""usage: python3 scripts/bench/link_impact.py <capture.ndjson>...

Per link-up window: records dropped, late dwells, nav_ms>=400, own bursts, gnss seconds missing."""
import pathlib, sys, collections, datetime
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records as r
def hm(s): return datetime.datetime.fromtimestamp(s, datetime.UTC).strftime("%H:%M:%S")
for f in sys.argv[1:]:
    recs=[x for x in r.read_ndjson(f) if x.get("type") not in r.NOTE_TYPES]
    build=next((x["fw_build"] for x in recs if x["type"]=="boot"),0)
    wins=[]; up=None
    for x in recs:
        if x["type"]=="link" and x["action"]=="up" and up is None: up=x["at_s"]
        if x["type"]=="link" and x["action"]=="down" and up is not None: wins.append((up,x["at_s"])); up=None
    if up is not None: wins.append((up, recs[-1]["at_s"]))
    # merge windows closer than 10 s
    m=[]
    for a,b in wins:
        if m and a-m[-1][1]<10: m[-1]=(m[-1][0],b)
        else: m.append((a,b))
    def inside(s): return any(a<=s<=b+2 for a,b in m)
    late=lambda d: d["start_ms"] in (205,400,800) and 1<d["phase_ms"]-d["start_ms"]<600 and not d["burst_armed"] and not (d["start_ms"]==205 and d["phase_ms"]>=700)
    c=collections.Counter()
    for x in recs:
        k="in" if inside(x["at_s"]) else "out"
        if x["type"]=="gap": c[k,"dropped"]+=x["dropped"]
        if x["type"]=="dwell" and late(x): c[k,"late_dwell"]+=1
        if x["type"]=="gnss" and x["nav_ms"]>=400: c[k,"nav>=400"]+=1
        if x["type"]=="gnss": c[k,"gnss_s"]+=1
    print("== %s build %d windows %s" % (f, build, [(hm(a), b-a) for a,b in m]))
    print("   ", dict(sorted(c.items())))
