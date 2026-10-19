"""nav_ms per screen page, and position windows missed per slot, against the build's windows.

usage: python3 scripts/bench/tx_check.py <capture.ndjson>...
"""
import bisect
import collections
import statistics
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records

WORD_MASK = 0xFFFFFFFF
SLOT = {0: (450, 790), 1: (800, 990)}
# INFO: fc 27sep26 8d27f2e (build 844) sized slot 0 against its dwell end, 790 became 787
# INFO: fc 29sep26 build 863 hop guard 3->2 ms, slot-0 dwell ends 798, last instant 790
SLOT0_LAST_FROM_BUILD = {844: 787, 856: 789, 863: 790, 874: 791}
SLOT1_LAST_FROM_BUILD = {856: 992, 863: 992, 874: 993}
SLOT0_ARMS_MS = 400


def mix(value):
    value ^= value >> 16
    value = (value * 0x7FEB352D) & WORD_MASK
    value ^= value >> 15
    value = (value * 0x846CA68B) & WORD_MASK
    value ^= value >> 16
    return value


def quantile(values, fraction):
    return values[min(len(values) - 1, int(len(values) * fraction))]


def page_lookup(records):
    screens = sorted((record["at_s"], record["into_ms"], record["page"]) for record in records if record["type"] == "screen")
    keys = [(second, into_ms) for second, into_ms, _ in screens]

    def page_at(at_s, into_ms=999):
        i = bisect.bisect_right(keys, (at_s, into_ms)) - 1
        return screens[i][2] if i >= 0 else None

    return page_at


def report(path):
    records = [record for record in blip_records.read_ndjson(path) if record.get("type") not in blip_records.NOTE_TYPES]
    address = next(record["address"] for record in records if record["type"] == "config")
    build = next((record["firmware_build"] for record in records if record["type"] == "boot"), 0)
    slots = dict(SLOT)
    for since, last in sorted(SLOT0_LAST_FROM_BUILD.items()):
        if build >= since:
            slots[0] = (450, last)
    for since, last in sorted(SLOT1_LAST_FROM_BUILD.items()):
        if build >= since:
            slots[1] = (800, last)
    ground = mix(address) % 10
    page_at = page_lookup(records)
    last_s = max(record["at_s"] for record in records)

    nav = {}
    for record in records:
        if record["type"] == "gnss" and record.get("utc_dated"):
            nav.setdefault(record["at_s"], record["solution_phase_ms"])
    sent = {record["at_s"] for record in records if record["type"] == "burst" and record["verdict"] == "transmitted"
            and not record["callsign"]}

    nav_by_page = collections.defaultdict(list)
    for second, nav_ms in nav.items():
        nav_by_page[page_at(second, nav_ms)].append(nav_ms)

    windows = collections.Counter()
    missed = collections.defaultdict(list)
    cut = 0
    for second in (second for second in nav if second % 10 == ground):
        slot = (second // 10) & 1
        first, last = slots[slot]
        instant = first + mix(address ^ mix(second)) % (last - first + 1)
        if second + 1 > last_s:
            cut += 1
            continue
        page = page_at(second, instant)
        windows[(page, slot)] += 1
        if not any(abs(sent_second - second) <= 1 for sent_second in sent):
            missed[(page, slot)].append(instant)

    print("== %s  addr %06X  build %d  %d s" % (path.split("/")[-1], address, build, len(nav)))
    for page in sorted(nav_by_page, key=lambda page_key: -1 if page_key is None else page_key):
        values = sorted(nav_by_page[page])
        late = sum(nav_ms >= SLOT0_ARMS_MS for nav_ms in values)
        print("  page %-4s %4d s  nav_ms median %4d  p99 %4d  max %4d  >= %d: %d" % (
            page, len(values), statistics.median(values), quantile(values, 0.99), values[-1],
            SLOT0_ARMS_MS, late))
        for slot in (0, 1):
            count = windows[(page, slot)]
            if count == 0:
                continue
            missed_instants = sorted(missed[(page, slot)])
            print("            slot %d  missed %d of %d  %s" % (slot, len(missed_instants), count, missed_instants[:12]))
    if cut:
        print("  %d window(s) cut off by the end of the capture, not counted" % cut)


for path in sys.argv[1:]:
    report(path)
