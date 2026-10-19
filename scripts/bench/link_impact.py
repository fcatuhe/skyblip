"""usage: python3 scripts/bench/link_impact.py <capture.ndjson>...

Per link-up window: records dropped, late dwells, nav_ms>=400, own bursts, gnss seconds missing."""
import pathlib, sys, collections, datetime
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records
def clock_text(second): return datetime.datetime.fromtimestamp(second, datetime.UTC).strftime("%H:%M:%S")
for path in sys.argv[1:]:
    records=[record for record in blip_records.read_ndjson(path) if record.get("type") not in blip_records.NOTE_TYPES]
    build=next((record["firmware_build"] for record in records if record["type"]=="boot"),0)
    windows=[]; up_at=None
    for record in records:
        if record["type"]=="link" and record["action"]=="up" and up_at is None: up_at=record["at_s"]
        if record["type"]=="link" and record["action"]=="down" and up_at is not None: windows.append((up_at,record["at_s"])); up_at=None
    if up_at is not None: windows.append((up_at, records[-1]["at_s"]))
    # merge windows closer than 10 s
    merged=[]
    for start,end in windows:
        if merged and start-merged[-1][1]<10: merged[-1]=(merged[-1][0],end)
        else: merged.append((start,end))
    def inside(second): return any(start<=second<=end+2 for start,end in merged)
    late=lambda dwell: dwell["start_ms"] in (205,400,800) and 1<dwell["phase_ms"]-dwell["start_ms"]<600 and not dwell["burst_armed"] and not (dwell["start_ms"]==205 and dwell["phase_ms"]>=700)
    counts=collections.Counter()
    for record in records:
        side="in" if inside(record["at_s"]) else "out"
        if record["type"]=="gap": counts[side,"dropped"]+=record["dropped"]
        if record["type"]=="dwell" and late(record): counts[side,"late_dwell"]+=1
        if record["type"]=="gnss" and record["solution_phase_ms"]>=400: counts[side,"nav>=400"]+=1
        if record["type"]=="gnss": counts[side,"gnss_s"]+=1
    print("== %s build %d windows %s" % (path, build, [(clock_text(start), end-start) for start,end in merged]))
    print("   ", dict(sorted(counts.items())))
