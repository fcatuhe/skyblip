"""blip.py's fetch subcommand: offload a store to NDJSON, resuming where a dead fetch stopped.

    ./scripts/blip.py fetch --log diagnostics --list
    ./scripts/blip.py fetch --log diagnostics --all --out capture.ndjson
"""
import json
import time

import blip_frames as frames
import blip_ndjson as ndjson
import blip_records as records

# INFO: fc 20sep26 comms::kMaxLogReadChunks, the clamp a read's "count" meets on the device
LOG_READ_CHUNKS_MAX = 8


def add_parser(sub, run):
    fetch = sub.add_parser("fetch", help="offload a store to NDJSON, resumably")
    fetch.add_argument("--log", default="flights", choices=records.STORES)
    fetch.add_argument("--session", type=int, help="one session id, by default the last one")
    fetch.add_argument("--all", action="store_true", help="every session the device lists")
    fetch.add_argument("--list", action="store_true", help="list the sessions and stop")
    fetch.add_argument("--out", help="NDJSON to append to, resuming where it stopped")
    fetch.add_argument("--restart", action="store_true", help="ignore what --out already holds")
    fetch.add_argument("--window", type=int, default=LOG_READ_CHUNKS_MAX,
                       help="chunks asked for per read, clamped to 1..%d as the device clamps it"
                            % LOG_READ_CHUNKS_MAX)
    fetch.add_argument("--timeout", type=float, default=5.0)
    fetch.set_defaults(run=run)


async def fetch_log(link, args):
    sessions = await list_sessions(link, args)
    for session in sessions:
        print(ndjson.session_text(session))
    if args.list:
        return 0

    kept = resume_of(args)
    resume = None
    if kept is not None:
        await refuse_if_renumbered(link, args, sessions, kept)
        resume = (kept["session"], kept["index"])
    wanted = sessions_to_fetch(sessions, args, resume)
    if not wanted:
        print("nothing to fetch")
        return 0
    out = open(args.out, "a", encoding="utf-8") if args.out else None
    try:
        for session, start in wanted:
            await fetch_session(link, args, session, start, out)
    finally:
        if out:
            out.close()
    return 0


def resume_of(args):
    if args.restart or not args.out:
        return None
    kept = ndjson.last_kept(args.out)
    if kept:
        print("resuming %s after session %d index %d" % (args.out, kept["session"], kept["index"]))
    return kept


async def refuse_if_renumbered(link, args, sessions, kept):
    # INFO: fc 20nov26 an index names a record only until the ring recycles the session's oldest sector
    session_id, index = kept["session"], kept["index"]
    if session_id not in [entry["session"] for entry in sessions]:
        return
    chunk, = await read_window(link, args, session_id, index, window=1)
    found = None
    if chunk.get("from") == index and chunk.get("records"):
        first = chunk["records"][:records.RECORD_BYTES]
        record, = records.decode_chunk(args.log, first, session_id, index)
        found = json.loads(json.dumps(dict(log=args.log, session=session_id, **record)))
    if found != kept:
        raise SystemExit("session %d no longer holds at index %d the record %s kept: the device "
                         "renumbered the session, as it does once its ring recycles the oldest "
                         "sector. Fetch it into a new file" % (session_id, index, args.out))


def sessions_to_fetch(sessions, args, resume):
    chosen = sessions
    if args.session is not None:
        chosen = [entry for entry in sessions if entry["session"] == args.session]
    elif not args.all:
        chosen = sessions_from(sessions, resume) or sessions[-1:]
    if resume is None:
        return [(entry, first_of(entry)) for entry in chosen]

    session_id, index = resume
    plan = []
    reached = False
    for entry in chosen:
        if entry["session"] == session_id:
            reached = True
            if index + 1 < end_of(entry) - held_back(args, entry):
                plan.append((entry, index + 1))
        elif reached:
            plan.append((entry, first_of(entry)))
    return plan if reached else [(entry, first_of(entry)) for entry in chosen]


def first_of(session):
    return session.get("first", 0)


def end_of(session):
    return first_of(session) + session["records"]


def held_back(args, session):
    return 1 if ndjson.tail_cannot_be_vouched_for(args.log, session) else 0


def sessions_from(sessions, resume):
    ids = [entry["session"] for entry in sessions]
    if resume is not None and resume[0] in ids:
        return sessions[ids.index(resume[0]):]
    else:
        return []


async def fetch_session(link, args, session, start, out):
    session_id = session["session"]
    total = end_of(session)
    began = time.monotonic()
    sink = ndjson.SessionSink(args.log, session, line_writer(out))
    raw_bytes = 0
    at = start
    while at < total:
        for chunk in await read_window(link, args, session_id, at):
            raw = chunk.get("records", b"")
            start_at = chunk.get("from")
            if start_at is None or start_at < at or (not raw and not chunk.get("ends_session")):
                raise SystemExit("session %d was asked for index %d and answered %s"
                                 % (session_id, at, frames.printable(chunk)))
            if start_at > at:
                print("session %d: indices %d..%d recycled before they were read"
                      % (session_id, at, start_at - 1))
                at = start_at
            raw_bytes += len(raw)
            decoded = records.decode_chunk(args.log, raw, session_id, at)
            for record in decoded:
                sink.write(record)
            at += len(decoded)
            if chunk.get("ends_session"):
                total = at
        if out:
            out.flush()
    sink.finish(complete=at >= total)
    if out:
        out.flush()

    span = max(time.monotonic() - began, 1e-6)
    print("session %d: %d records kept, %d unreadable, %.0f rec/s, %.1f kB/s of records"
          % (session_id, sink.kept, sink.unreadable, (sink.kept + sink.unreadable) / span,
             raw_bytes / span / 1024))
    if sink.torn is not None:
        print("session %d: index %d dropped, %s" % (session_id, sink.torn["index"],
                                                    ndjson.TORN_TAIL_REASON))
    if at < end_of(session):
        print("session %d: stopped at %d of %d" % (session_id, at, end_of(session)))


def line_writer(out):
    if out is None:
        return lambda line: print(ndjson.render(line))
    return lambda line: out.write(json.dumps(line) + "\n")


async def read_window(link, args, session_id, at, window=None):
    asked = chunks_per_read(args.window if window is None else window)
    await link.write({"command": "read", "log": args.log, "session": session_id,
                      "from": at, "count": asked})
    chunks = []
    while len(chunks) < asked:
        frame = frames.refuse_if_nacked(await link.frame(args.timeout))
        if frame.get("command") != "chunk":
            raise SystemExit("unexpected reply to read: %s" % frames.printable(frame))
        chunks.append(frame)
        if frame.get("ends_session"):
            break
    return chunks


def chunks_per_read(window):
    # INFO: fc 20nov26 the device's own clamp, so the host waits for exactly the frames this read brings
    if window <= 0:
        return 1
    return min(window, LOG_READ_CHUNKS_MAX)


async def list_sessions(link, args):
    await link.write({"command": "list", "log": args.log})
    count = frames.refuse_if_nacked(await link.frame(args.timeout))
    sessions = []
    for index in range(count.get("sessions", 0)):
        await link.write({"command": "list", "log": args.log, "index": index})
        sessions.append(frames.refuse_if_nacked(await link.frame(args.timeout)))
    if count.get("truncated"):
        print("the device holds more sessions than its index offers")
    return sessions
