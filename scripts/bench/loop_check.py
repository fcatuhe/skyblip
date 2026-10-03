"""Where the service loop's time goes, per screen page, from build 876's loop, screen and burst fields.

usage: python3 scripts/bench/loop_check.py <capture.ndjson>...

Per page: passes a second, busy ms a second, the longest pass and gap and which service held it,
the render and present cost of the page, and how far behind PPS the fix was processed. Then every
second whose longest gap or pass passed 100 ms, and the stage margin of every burst sent, against
how late it was keyed.
"""
import collections
import pathlib
import statistics as st
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records as r

SLOW_MS = 100


def spread(values):
    if not values:
        return "n=0"
    v = sorted(values)
    return "n=%d med %s p99 %s max %s" % (len(v), v[len(v) // 2], v[int(len(v) * 0.99)], v[-1])


def report(path):
    recs = [x for x in r.read_ndjson(path) if x.get("type") not in r.NOTE_TYPES]
    builds = sorted({x["fw_build"] for x in recs if x["type"] == "boot"})
    print("== %s  build %s" % (path.split("/")[-1], builds))
    loops = [x for x in recs if x["type"] == "loop"]
    if not loops:
        print("   no loop records: this build predates them")
        return

    page = None
    page_of = {}
    nav_of = {}
    by_page = collections.defaultdict(lambda: collections.defaultdict(list))
    for x in recs:
        if x["type"] == "screen":
            page = x["page"]
            if x["render_us"]:
                by_page[page]["render_ms"].append(round(x["render_us"] / 1000, 1))
            if x["present_us"]:
                by_page[page]["present_ms"].append(round(x["present_us"] / 1000, 1))
        if x["type"] == "gnss":
            nav_of[x["at_s"]] = x["nav_ms"]
            by_page[page]["nav_ms"].append(x["nav_ms"])
        if x["type"] == "loop":
            page_of[x["at_s"]] = page

    for x in loops:
        p = page_of[x["at_s"]]
        by_page[p]["passes"].append(x["passes"])
        by_page[p]["busy_ms"].append(x["busy_ms"])
        by_page[p]["worst_pass_ms"].append(x["worst_pass_us"] // 1000)
        by_page[p]["worst_gap_ms"].append(x["worst_gap_ms"])
        by_page[p]["holder"].append(x["worst_service"])

    for p in sorted(by_page, key=lambda k: -1 if k is None else k):
        d = by_page[p]
        if not d["passes"]:
            continue
        print("   page %s  %d s" % (p, len(d["passes"])))
        for key in ("passes", "busy_ms", "worst_pass_ms", "worst_gap_ms", "render_ms", "present_ms", "nav_ms"):
            print("     %-14s %s" % (key, spread(d[key])))
        print("     holder         %s" % dict(collections.Counter(d["holder"]).most_common()))

    slow = [x for x in loops if x["worst_gap_ms"] >= SLOW_MS or x["worst_pass_us"] >= SLOW_MS * 1000]
    print("   seconds with a pass or a gap >= %d ms: %d of %d" % (SLOW_MS, len(slow), len(loops)))
    for x in slow[:40]:
        print("     at %d  page %s  pass %5.1f ms at %3d  gap %3d ms at %3d  holder %s %d ms  nav %s" % (
            x["at_s"], page_of[x["at_s"]], x["worst_pass_us"] / 1000, x["worst_pass_phase_ms"],
            x["worst_gap_ms"], x["worst_gap_phase_ms"], x["worst_service"], x["worst_tick_ms"],
            nav_of.get(x["at_s"] - 1)))

    sent = [x for x in recs if x["type"] == "burst" and x["verdict"] == "transmitted"]
    if sent:
        print("   stage margin us  %s" % spread([x["tx_stage_margin_us"] for x in sent]))
        late = [x for x in sent if x["tx_keyed_us"] > 130]
        print("   keyed > 130 us: %d of %d, their stage margin %s" % (
            len(late), len(sent), [x["tx_stage_margin_us"] for x in late]))


def main():
    for path in sys.argv[1:]:
        report(path)


if __name__ == "__main__":
    main()
