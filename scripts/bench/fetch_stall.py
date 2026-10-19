"""usage: python3 scripts/bench/fetch_stall.py <capture.ndjson>...

Fetch window in 10 s buckets: late dwells, nav_ms, gap drops, and how long the loop went quiet after each log command."""
import pathlib, sys, collections, statistics
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records
def instant_ms(record): return record["at_s"] * 1000 + record["into_ms"]
late = lambda dwell: dwell["start_ms"] in (205, 400, 800) and 1 < dwell["phase_ms"] - dwell["start_ms"] < 600 and not dwell["burst_armed"] and not (dwell["start_ms"] == 205 and dwell["phase_ms"] >= 700)
for path in sys.argv[1:]:
    records = [record for record in blip_records.read_ndjson(path) if record.get("type") not in blip_records.NOTE_TYPES]
    commands = [i for i, record in enumerate(records) if record["type"] == "link" and record["action"] == "received" and record["endpoint"] == "log"]
    if not commands:
        print("== %s: no log commands" % path); continue
    first_s = records[commands[0]]["at_s"]; last_s = records[commands[-1]]["at_s"]
    quiet = collections.defaultdict(list)
    for i in commands:
        command_ms = instant_ms(records[i])
        next_ms = next((instant_ms(following) for following in records[i + 1:i + 80] if following["type"] not in ("switch", "pps") and instant_ms(following) > command_ms + 1), None)
        if next_ms: quiet[(records[i]["at_s"] - first_s) // 10].append(next_ms - command_ms)
    late_by_bucket = collections.Counter(); nav_by_bucket = collections.defaultdict(list); dropped_by_bucket = collections.Counter()
    for record in records:
        if not (first_s <= record["at_s"] <= last_s + 2): continue
        bucket = (record["at_s"] - first_s) // 10
        if record["type"] == "dwell" and late(record): late_by_bucket[bucket] += 1
        if record["type"] == "gnss": nav_by_bucket[bucket].append(record["nav_ms"])
        if record["type"] == "gap": dropped_by_bucket[bucket] += record["dropped"]
    print("== %s: %d log commands over %d s" % (path.split("/")[-1], len(commands), last_s - first_s))
    print("   t_s  cmds quiet_med quiet_max late nav_med nav_max dropped")
    for bucket in sorted(set(quiet) | set(nav_by_bucket)):
        quiet_ms = quiet.get(bucket, []); nav_ms = nav_by_bucket.get(bucket, [])
        print("  %4d  %4d  %8s  %8s  %4d  %6s  %6s  %6d" % (bucket * 10, len(quiet_ms), int(statistics.median(quiet_ms)) if quiet_ms else "-", max(quiet_ms) if quiet_ms else "-", late_by_bucket[bucket], int(statistics.median(nav_ms)) if nav_ms else "-", max(nav_ms) if nav_ms else "-", dropped_by_bucket[bucket]))
