"""nav_ms per screen page, and position windows missed per slot, against the build's windows.

usage: python3 scripts/bench/tx_check.py <capture.ndjson>...
"""
import bisect
import collections
import statistics
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records as r

M = 0xFFFFFFFF
SLOT = {0: (450, 790), 1: (800, 990)}
# INFO: fc 27sep26 8d27f2e (build 844) sized slot 0 against its dwell end, 790 became 787
# INFO: fc 29sep26 build 863 hop guard 3->2 ms, slot-0 dwell ends 798, last instant 790
SLOT0_LAST_FROM_BUILD = {844: 787, 856: 789, 863: 790, 874: 791}
SLOT1_LAST_FROM_BUILD = {856: 992, 863: 992, 874: 993}
SLOT0_ARMS_MS = 400


def mix(x):
    x ^= x >> 16
    x = (x * 0x7FEB352D) & M
    x ^= x >> 15
    x = (x * 0x846CA68B) & M
    x ^= x >> 16
    return x


def quantile(values, p):
    return values[min(len(values) - 1, int(len(values) * p))]


def page_lookup(recs):
    screens = sorted((x["at_s"], x["into_ms"], x["page"]) for x in recs if x["type"] == "screen")
    keys = [(s, ms) for s, ms, _ in screens]

    def page_at(at_s, into_ms=999):
        i = bisect.bisect_right(keys, (at_s, into_ms)) - 1
        return screens[i][2] if i >= 0 else None

    return page_at


def report(path):
    recs = [x for x in r.read_ndjson(path) if x.get("type") not in r.NOTE_TYPES]
    addr = next(x["addr"] for x in recs if x["type"] == "config")
    build = next((x["fw_build"] for x in recs if x["type"] == "boot"), 0)
    slots = dict(SLOT)
    for since, last in sorted(SLOT0_LAST_FROM_BUILD.items()):
        if build >= since:
            slots[0] = (450, last)
    for since, last in sorted(SLOT1_LAST_FROM_BUILD.items()):
        if build >= since:
            slots[1] = (800, last)
    ground = mix(addr) % 10
    page_at = page_lookup(recs)
    last_s = max(x["at_s"] for x in recs)

    nav = {}
    for x in recs:
        if x["type"] == "gnss" and x.get("utc_dated"):
            nav.setdefault(x["at_s"], x["nav_ms"])
    sent = {x["at_s"] for x in recs if x["type"] == "burst" and x["verdict"] == "transmitted"
            and not x["callsign"]}

    nav_by_page = collections.defaultdict(list)
    for s, ms in nav.items():
        nav_by_page[page_at(s, ms)].append(ms)

    windows = collections.Counter()
    missed = collections.defaultdict(list)
    cut = 0
    for s in (s for s in nav if s % 10 == ground):
        slot = (s // 10) & 1
        first, last = slots[slot]
        instant = first + mix(addr ^ mix(s)) % (last - first + 1)
        if s + 1 > last_s:
            cut += 1
            continue
        page = page_at(s, instant)
        windows[(page, slot)] += 1
        if not any(abs(t - s) <= 1 for t in sent):
            missed[(page, slot)].append(instant)

    print("== %s  addr %06X  build %d  %d s" % (path.split("/")[-1], addr, build, len(nav)))
    for page in sorted(nav_by_page, key=lambda p: -1 if p is None else p):
        values = sorted(nav_by_page[page])
        late = sum(v >= SLOT0_ARMS_MS for v in values)
        print("  page %-4s %4d s  nav_ms median %4d  p99 %4d  max %4d  >= %d: %d" % (
            page, len(values), statistics.median(values), quantile(values, 0.99), values[-1],
            SLOT0_ARMS_MS, late))
        for slot in (0, 1):
            n = windows[(page, slot)]
            if n == 0:
                continue
            m = sorted(missed[(page, slot)])
            print("            slot %d  missed %d of %d  %s" % (slot, len(m), n, m[:12]))
    if cut:
        print("  %d window(s) cut off by the end of the capture, not counted" % cut)


for path in sys.argv[1:]:
    report(path)
