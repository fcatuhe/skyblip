"""Write and read the NDJSON a fetch keeps: the session notes, the torn tail and the summary.

The session lines are the ones server/app/javascript/skyblip/diagnostics.js writes for the
same capture, and its test holds the two to the same NDJSON, so change both together.
"""
import datetime
import json


NOTE_TYPES = ("session", "torn")
TORN_TAIL_REASON = "the last record of a session the device did not close may be a torn write"


def is_note(line):
    return line.get("type") in NOTE_TYPES


def session_text(session):
    marks = ["closed" if session.get("closed") else "recording" if session.get("recording")
             else "power cut"]
    if session.get("truncated"):
        marks.append("truncated")
    start = " from index %d" % session["first"] if session.get("first") else ""
    return "session %d  %d records%s  %s" % (session["session"], session["records"], start,
                                             ", ".join(marks))


def session_note(log, session):
    note = {"type": "session", "log": log, "session": session["session"],
            "records": session["records"], "closed": bool(session.get("closed")),
            "truncated": bool(session.get("truncated"))}
    if session.get("recording"):
        note["recording"] = True
    return note


def tail_cannot_be_vouched_for(log, session):
    # INFO: fc 20sep26 a diagnostics record carries no CRC, so a power cut leaves a plausible slot
    return log == "diagnostics" and not session.get("closed", True) and not session.get("recording")


class SessionSink:

    def __init__(self, log, session, emit):
        self.log = log
        self.session = session["session"]
        self.emit = emit
        self.drops_tail = tail_cannot_be_vouched_for(log, session)
        self.kept = 0
        self.unreadable = 0
        self.torn = None
        self.held = None
        emit(session_note(log, session))

    def write(self, record):
        self.release()
        self.held = dict(log=self.log, session=self.session, **record)

    def finish(self, complete):
        if self.held is None or not (complete and self.drops_tail):
            self.release()
            return
        self.torn, self.held = self.held, None
        self.emit({"type": "torn", "log": self.log, "session": self.session,
                   "index": self.torn["index"], "reason": TORN_TAIL_REASON})

    def release(self):
        if self.held is None:
            return
        if self.held.get("error"):
            self.unreadable += 1
        else:
            self.kept += 1
        self.emit(self.held)
        self.held = None


def read_ndjson(path):
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if line.strip():
                yield json.loads(line)


def last_kept(path):
    last = None
    try:
        for line in read_ndjson(path):
            if not is_note(line) and "session" in line and "index" in line:
                last = line
    except (OSError, json.JSONDecodeError):
        return last
    return last


def stamp_of(record):
    # INFO: fc 20nov26 into_ms reaches 1200 on a slot 1 burst, so it is printed beside the second
    if "utc_s" in record:
        return utc_text(record["utc_s"])
    second = utc_text(record["at_s"]) if record.get("utc_dated") else "+%ds" % record.get("at_s", 0)
    return "%s %dms" % (second, record.get("into_ms", 0))


def utc_text(seconds):
    moment = datetime.datetime.fromtimestamp(seconds, datetime.timezone.utc)
    return moment.strftime("%Y-%m-%dT%H:%M:%S")


ENVELOPE = ("index", "session", "log", "type", "at_s", "into_ms", "utc_s")


def render(record):
    body = " ".join("%s=%s" % (key, value) for key, value in record.items()
                    if key not in ENVELOPE)
    stamp = "" if is_note(record) else stamp_of(record)
    return "%6s %-28s %-10s %s" % (record.get("index", ""), stamp,
                                   record.get("type", "?"), body)


def summarise(lines_in):
    counts = {}
    stamped = []
    gaps = []
    unreadable = []
    end = None
    records = [line for line in lines_in if not is_note(line)]
    for record in records:
        counts[record.get("type", "?")] = counts.get(record.get("type", "?"), 0) + 1
        moment = record.get("utc_s", record.get("at_s"))
        if moment is not None:
            stamped.append((moment, record))
        if record.get("type") == "gap":
            gaps.append(record)
        if record.get("type") == "end":
            end = record
        if record.get("error"):
            unreadable.append(record)

    lines = caveat_lines([line for line in lines_in if is_note(line)], records)
    lines.append("%d records" % sum(counts.values()))
    if stamped:
        first = min(stamped, key=lambda pair: pair[0])
        last = max(stamped, key=lambda pair: pair[0])
        lines.append("span %s to %s, %d s" % (stamp_of(first[1]), stamp_of(last[1]),
                                              last[0] - first[0]))
    for kind in sorted(counts):
        lines.append("  %-12s %d" % (kind, counts[kind]))
    lines.extend(missing_index_lines(records))
    for gap in gaps:
        lines.append("gap at %s: %s records dropped over %s ms, %s since arming, ring holds %s" %
                     (stamp_of(gap), gap.get("dropped"), gap.get("span_ms"), gap.get("total"),
                      gap.get("capacity")))
    for bad in unreadable:
        lines.append("unreadable at index %s: %s" % (bad.get("index"), bad.get("error")))
    if not gaps and not unreadable:
        lines.append("no gap records and no unreadable slots")
    if end is not None:
        lines.append("device reports %s records written, %s dropped" %
                     (end.get("records"), end.get("dropped")))
    return lines


def caveat_lines(notes, records):
    kept = {(record.get("session"), record.get("index")) for record in records}
    out = []
    for note in notes:
        if note["type"] == "torn" and (note["session"], note.get("index")) in kept:
            continue
        for line in caveats_of(note):
            if line not in out:
                out.append(line)
    return out


def caveats_of(note):
    session = note["session"]
    if note["type"] == "torn":
        return ["session %s: index %s dropped, %s" % (session, note.get("index"),
                                                      note.get("reason", TORN_TAIL_REASON))]
    out = []
    if note.get("truncated"):
        out.append("session %s: truncated, its first sector was evicted, so it carries no boot "
                   "and no config record" % session)
    if note.get("recording"):
        out.append("session %s: still recording when fetched" % session)
    elif not note.get("closed"):
        out.append("session %s: not closed, the device stopped without ending it" % session)
    return out


def missing_index_lines(records):
    seen = sorted(record["index"] for record in records if "index" in record)
    holes = [(first, last) for first, last in zip(seen, seen[1:]) if last > first + 1]
    return ["missing indices %d..%d" % (first + 1, last - 1) for first, last in holes]
