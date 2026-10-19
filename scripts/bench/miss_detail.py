"""Every missed position window of one capture, with the records of the seconds around it.

usage: python3 scripts/bench/miss_detail.py <capture.ndjson>
"""
import pathlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records
WORD_MASK = 0xFFFFFFFF
from bench_check import windows_for
def mix(value):
    value ^= value >> 16; value = (value * 0x7FEB352D) & WORD_MASK; value ^= value >> 15; value = (value * 0x846CA68B) & WORD_MASK; value ^= value >> 16
    return value
path = sys.argv[1]
records = [record for record in blip_records.read_ndjson(path) if record.get("type") not in blip_records.NOTE_TYPES]
address = next(record["address"] for record in records if record["type"] == "config")
SLOT = windows_for(next((record["fw_build"] for record in records if record["type"] == "boot"), 0))
ground = mix(address) % 10
seconds = sorted({record["at_s"] for record in records if record["type"] == "gnss"})
sent = {record["at_s"] for record in records if record["type"] == "burst" and record["verdict"] == "transmitted" and not record["callsign"]}
for second in seconds:
    if second % 10 != ground: continue
    if any(abs(sent_second - second) <= 1 for sent_second in sent): continue
    slot = (second // 10) & 1
    first, last = SLOT[slot]
    instant = first + mix(address ^ mix(second)) % (last - first + 1)
    print(f"--- miss s={second} slot {slot} instant {instant}")
    for record in records:
        if second - 1 <= record["at_s"] <= second + 1 and record["type"] in ("burst", "gnss", "dwell", "link", "pps", "screen"):
            keep = {key: record[key] for key in ("type", "into_ms", "nav_ms", "fix_valid", "reject", "stage", "verdict", "callsign", "band", "start_ms", "end_ms", "phase_ms", "state", "refusal", "armed", "burst_armed", "tx_keyed_us", "action", "since_edge_ms", "page") if key in record}
            print(f"  {record['at_s']-second:+d} ", keep)
