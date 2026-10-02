"""Every missed position window of one capture, with the records of the seconds around it.

usage: python3 scripts/bench/miss_detail.py <capture.ndjson>
"""
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records as r
M = 0xFFFFFFFF
from bench_check import windows_for
def mix(x):
    x ^= x >> 16; x = (x * 0x7FEB352D) & M; x ^= x >> 15; x = (x * 0x846CA68B) & M; x ^= x >> 16
    return x
path = sys.argv[1]
recs = [x for x in r.read_ndjson(path) if x.get("type") not in r.NOTE_TYPES]
addr = next(x["addr"] for x in recs if x["type"] == "config")
SLOT = windows_for(next((x["fw_build"] for x in recs if x["type"] == "boot"), 0))
ground = mix(addr) % 10
secs = sorted({x["at_s"] for x in recs if x["type"] == "gnss"})
sent = {x["at_s"] for x in recs if x["type"] == "burst" and x["verdict"] == "transmitted" and not x["callsign"]}
for s in secs:
    if s % 10 != ground: continue
    if any(abs(t - s) <= 1 for t in sent): continue
    slot = (s // 10) & 1
    first, last = SLOT[slot]
    inst = first + mix(addr ^ mix(s)) % (last - first + 1)
    print(f"--- miss s={s} slot {slot} instant {inst}")
    for x in recs:
        if s - 1 <= x["at_s"] <= s + 1 and x["type"] in ("burst", "gnss", "dwell", "link", "pps", "screen"):
            keep = {k: x[k] for k in ("type", "into_ms", "nav_ms", "fix_valid", "reject", "stage", "verdict", "callsign", "band", "start_ms", "end_ms", "phase_ms", "state", "refusal", "armed", "burst_armed", "tx_keyed_us", "action", "since_edge_ms", "page") if k in x}
            print(f"  {x['at_s']-s:+d} ", keep)
