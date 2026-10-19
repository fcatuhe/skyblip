"""Where the service loop's time goes, per screen page, from build 876's loop, screen and burst series.

usage: python3 scripts/bench/loop_check.py <capture.ndjson>...

Per page: passes a second, busy ms a second, the longest pass and gap and which service held it,
the render and present cost of the page, and how far behind PPS the fix was processed. Then every
second whose longest gap or pass passed 100 ms, and the stage margin of every burst sent, against
how late it was keyed.
"""
import collections
import pathlib
import statistics
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records

SLOW_MS = 100


def spread(values):
    if not values:
        return "n=0"
    ordered = sorted(values)
    return "n=%d med %s p99 %s max %s" % (len(ordered), ordered[len(ordered) // 2], ordered[int(len(ordered) * 0.99)], ordered[-1])


def report(path):
    records = [record for record in blip_records.read_ndjson(path) if record.get("type") not in blip_records.NOTE_TYPES]
    builds = sorted({record["firmware_build"] for record in records if record["type"] == "boot"})
    print("== %s  build %s" % (path.split("/")[-1], builds))
    loops = [record for record in records if record["type"] == "loop"]
    if not loops:
        print("   no loop records: this build predates them")
        return

    page = None
    page_of = {}
    nav_of = {}
    by_page = collections.defaultdict(lambda: collections.defaultdict(list))
    for record in records:
        if record["type"] == "screen":
            page = record["page"]
            if record["render_us"]:
                by_page[page]["render_ms"].append(round(record["render_us"] / 1000, 1))
            if record["present_us"]:
                by_page[page]["present_ms"].append(round(record["present_us"] / 1000, 1))
        if record["type"] == "gnss":
            nav_of[record["at_s"]] = record["solution_phase_ms"]
            by_page[page]["solution_phase_ms"].append(record["solution_phase_ms"])
        if record["type"] == "loop":
            page_of[record["at_s"]] = page

    for record in loops:
        shown_page = page_of[record["at_s"]]
        by_page[shown_page]["passes"].append(record["passes"])
        by_page[shown_page]["busy_ms"].append(record["busy_ms"])
        by_page[shown_page]["worst_pass_ms"].append(record["worst_pass_us"] // 1000)
        by_page[shown_page]["worst_gap_ms"].append(record["worst_gap_ms"])
        by_page[shown_page]["holder"].append(record["worst_service"])

    for shown_page in sorted(by_page, key=lambda page_key: -1 if page_key is None else page_key):
        series = by_page[shown_page]
        if not series["passes"]:
            continue
        print("   page %s  %d s" % (shown_page, len(series["passes"])))
        for key in ("passes", "busy_ms", "worst_pass_ms", "worst_gap_ms", "render_ms", "present_ms", "solution_phase_ms"):
            print("     %-14s %s" % (key, spread(series[key])))
        print("     holder         %s" % dict(collections.Counter(series["holder"]).most_common()))

    slow = [record for record in loops if record["worst_gap_ms"] >= SLOW_MS or record["worst_pass_us"] >= SLOW_MS * 1000]
    print("   seconds with a pass or a gap >= %d ms: %d of %d" % (SLOW_MS, len(slow), len(loops)))
    for record in slow[:40]:
        print("     at %d  page %s  pass %5.1f ms at %3d  gap %3d ms at %3d  holder %s %d ms  nav %s" % (
            record["at_s"], page_of[record["at_s"]], record["worst_pass_us"] / 1000, record["worst_pass_phase_ms"],
            record["worst_gap_ms"], record["worst_gap_phase_ms"], record["worst_service"], record["worst_tick_ms"],
            nav_of.get(record["at_s"] - 1)))

    sent = [record for record in records if record["type"] == "burst" and record["verdict"] == "transmitted"]
    if sent:
        print("   stage margin us  %s" % spread([record["tx_stage_margin_us"] for record in sent]))
        late = [record for record in sent if record["tx_keyed_us"] > 130]
        print("   keyed > 130 us: %d of %d, their stage margin %s" % (
            len(late), len(sent), [record["tx_stage_margin_us"] for record in late]))


def main():
    for path in sys.argv[1:]:
        report(path)


if __name__ == "__main__":
    main()
