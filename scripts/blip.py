#!/usr/bin/env -S uv run --script --with bleak
"""Bench CLI for a skyBlip over BLE: scan, stream, cmd, bench, fetch, decode.

    ./scripts/blip.py scan
    ./scripts/blip.py stream --seconds 20
    ./scripts/blip.py cmd '{"cmd":"diag"}'
    ./scripts/blip.py bench
    ./scripts/blip.py fetch --log diagnostics --all --out capture.ndjson
    ./scripts/blip.py decode capture.ndjson --summary

The shebang runs it through uv, which installs bleak into a throwaway
environment, so there is no venv to create and nothing to keep up to date.
macOS talks to the radio through CoreBluetooth and asks for Bluetooth
permission the first time; Linux talks to BlueZ over D-Bus, and Linux is the
side to use for anything that needs two centrals at once.
"""
import argparse
import asyncio
import json
import statistics
import sys
import time

import blip_records as records

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
HM10_SERVICE = "0000ffe0-0000-1000-8000-00805f9b34fb"
CONFIG_CHAR = "69c21302-0187-4204-a91d-02eb8858b440"
LOG_CHAR = "69c21303-0187-4204-a91d-02eb8858b440"
NAME_PREFIXES = ("skyBlip", "SoftRF")
SERVICE_UUIDS = (NUS_SERVICE, HM10_SERVICE)
# INFO: fc 20sep26 comms::kLogReadChunksMax, the clamp a read's "count" meets on the device
LOG_READ_CHUNKS_MAX = 8


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--name", default="skyBlip", help="advertised name prefix to connect to")
    parser.add_argument("--address", help="connect to this address and skip the name scan")
    parser.add_argument("--scan-seconds", type=float, default=6.0)
    sub = parser.add_subparsers(dest="subcommand", required=True)

    scan = sub.add_parser("scan", help="devices advertising Nordic UART or HM-10")
    scan.add_argument("--seconds", type=float, default=6.0)
    scan.add_argument("--all", action="store_true", help="every advertiser, not only ours")
    scan.set_defaults(run=run_scan)

    stream = sub.add_parser("stream", help="the NMEA traffic picture, checksum verified")
    stream.add_argument("--seconds", type=float, default=20.0)
    stream.set_defaults(run=run_stream)

    one = sub.add_parser("cmd", help="one config command, all of its reply frames")
    one.add_argument("json")
    one.add_argument("--timeout", type=float, default=4.0)
    one.set_defaults(run=run_cmd)

    bench = sub.add_parser("bench", help="connect, round trip, throughput, cadence, jitter")
    bench.add_argument("--seconds", type=float, default=20.0)
    bench.add_argument("--samples", type=int, default=10)
    bench.add_argument("--timeout", type=float, default=5.0)
    bench.set_defaults(run=run_bench)

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
    fetch.set_defaults(run=run_fetch)

    decode = sub.add_parser("decode", help="a fetched NDJSON as text")
    decode.add_argument("path")
    decode.add_argument("--summary", action="store_true")
    decode.add_argument("--type", help="only records of this type")
    decode.set_defaults(run=run_decode)

    args = parser.parse_args()
    if args.run is run_decode:
        return run_decode(args)
    return asyncio.run(args.run(args))


def run_decode(args):
    kept = [record for record in records.read_ndjson(args.path)
            if args.type is None or record.get("type") == args.type or records.is_note(record)]
    if args.summary:
        print("\n".join(records.summarise(kept)))
        return 0
    for record in kept:
        print(records.render(record))
    return 0


async def run_scan(args):
    seen = await bleak_scanner().discover(timeout=args.seconds, return_adv=True)
    for device, advert in sorted(seen.values(), key=lambda pair: -(pair[1].rssi or -999)):
        if not args.all and not is_a_blip(advert):
            continue
        print("%5s dBm  %-24s %s" % (advert.rssi, advert.local_name or "(unnamed)", device.address))
        for uuid in advert.service_uuids:
            print("            %s" % uuid)
    return 0


def is_a_blip(advert):
    if advert.local_name and advert.local_name.startswith(NAME_PREFIXES):
        return True
    return any(uuid.lower() in SERVICE_UUIDS for uuid in advert.service_uuids)


async def run_stream(args):
    async with await connect(args, stream=True) as link:
        print("# %s, payload %s bytes" % (link.address, link.payload_bytes()))
        deadline = time.monotonic() + args.seconds
        async for at, line in link.stream_lines(deadline):
            flag = "" if nmea_checksum_ok(line) else "   <- BAD CHECKSUM"
            print("%s %s%s" % (host_time(at), line, flag))
    return 0


async def run_cmd(args):
    async with await connect(args, config=True) as link:
        for delay, text in await link.command(json.loads(args.json), args.timeout):
            print("%7.1f ms  %s" % (delay * 1000, text))
    return 0


async def run_bench(args):
    async with await connect(args, stream=True, config=True) as link:
        print("device      %s" % link.address)
        print("connect     %.0f ms to GATT ready" % link.connect_ms)
        print("mtu         %s bytes negotiated, %s of payload" % (link.mtu(), link.payload_bytes()))

        trips = []
        for _ in range(args.samples):
            (delay, _), = await link.command({"cmd": "status"}, args.timeout)
            trips.append(delay * 1000)
        report_spread("status rtt", trips, "ms")

        began = time.monotonic()
        frames = await link.command({"cmd": "diag"}, args.timeout)
        span = max(time.monotonic() - began, 1e-6)
        size = sum(len(text) for _, text in frames)
        print("diag        %d frames, %d bytes in %.0f ms (%.1f kB/s)"
              % (len(frames), size, span * 1000, size / span / 1024))

        link.forget_stream()
        deadline = time.monotonic() + args.seconds
        kinds, bad, heads = {}, [], []
        async for at, line in link.stream_lines(deadline):
            talker = line.split(",")[0]
            kinds[talker] = kinds.get(talker, 0) + 1
            if not nmea_checksum_ok(line):
                bad.append(line)
            if talker == "$PFLAU":
                heads.append(at)
        print("stream      %d sentences in %.0fs, %d bad checksums"
              % (sum(kinds.values()), args.seconds, len(bad)))
        print("            %s" % " ".join("%s=%d" % pair for pair in sorted(kinds.items())))
        report_spread("cadence", [(b - a) * 1000 for a, b in zip(heads, heads[1:])], "ms")

        sizes = [size for _, size in link.notifications]
        if sizes:
            print("notify      %d frames, min %d median %d max %d bytes"
                  % (len(sizes), min(sizes), statistics.median(sizes), max(sizes)))
        report_spread("notify gap",
                      [(b - a) * 1000 for (a, _), (b, _) in
                       zip(link.notifications, link.notifications[1:])], "ms")
    return 0


def report_spread(label, values, unit):
    if not values:
        return
    print("%-11s n=%d min %.1f median %.1f max %.1f %s, sd %.1f %s"
          % (label, len(values), min(values), statistics.median(values), max(values), unit,
             statistics.pstdev(values), unit))


async def run_fetch(args):
    async with await connect(args, log=True) as link:
        sessions = await list_sessions(link, args)
        for session in sessions:
            print(records.session_text(session))
        if args.list:
            return 0

        wanted = sessions_to_fetch(sessions, args, resume_of(args))
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
    point = records.resume_point(args.out)
    if point:
        print("resuming %s after session %d index %d" % (args.out, point[0], point[1]))
    return point


def sessions_to_fetch(sessions, args, resume):
    """Which sessions, and the index each starts at once a dead fetch is taken into account."""
    chosen = sessions
    if args.session is not None:
        chosen = [entry for entry in sessions if entry["session"] == args.session]
    elif not args.all:
        chosen = sessions_from(sessions, resume) or sessions[-1:]
    if resume is None:
        return [(entry, 0) for entry in chosen]

    session_id, index = resume
    plan = []
    reached = False
    for entry in chosen:
        if entry["session"] == session_id:
            reached = True
            if index + 1 < entry["records"]:
                plan.append((entry, index + 1))
        elif reached:
            plan.append((entry, 0))
    return plan if reached else [(entry, 0) for entry in chosen]


def sessions_from(sessions, resume):
    """The session a resume point names and every one after it, or none if it is not listed."""
    ids = [entry["session"] for entry in sessions]
    if resume is not None and resume[0] in ids:
        return sessions[ids.index(resume[0]):]
    else:
        return []


async def fetch_session(link, args, session, start, out):
    session_id = session["session"]
    total = session["records"]
    began = time.monotonic()
    sink = records.SessionSink(args.log, session, line_writer(out))
    encoded_bytes = 0
    at = start
    while at < total:
        for chunk in await read_window(link, args, session_id, at):
            if chunk["from"] != at or (chunk["n"] == 0 and not chunk.get("eof")):
                raise SystemExit("session %d was asked for index %d and answered %s"
                                 % (session_id, at, json.dumps(chunk)))
            encoded_bytes += len(chunk.get("data", ""))
            for record in records.decode_chunk(args.log, chunk.get("data", ""), session_id, at):
                sink.write(record)
            at += chunk["n"]
            if chunk.get("eof"):
                total = at
        if out:
            out.flush()
    sink.finish(complete=at >= total)
    if out:
        out.flush()

    span = max(time.monotonic() - began, 1e-6)
    print("session %d: %d records kept, %d unreadable, %.0f rec/s, %.1f kB/s of base64"
          % (session_id, sink.kept, sink.unreadable, (sink.kept + sink.unreadable) / span,
             encoded_bytes / span / 1024))
    if sink.torn is not None:
        print("session %d: index %d dropped, %s" % (session_id, sink.torn["index"],
                                                    records.TORN_TAIL_REASON))
    if at < session["records"]:
        print("session %d: stopped at %d of %d" % (session_id, at, session["records"]))


def line_writer(out):
    if out is None:
        return lambda line: print(records.render(line))
    return lambda line: out.write(json.dumps(line) + "\n")


async def read_window(link, args, session_id, at):
    """One read command, and exactly the chunk frames the device answers it with."""
    asked = chunks_per_read(args.window)
    await link.write({"cmd": "read", "log": args.log, "session": session_id,
                      "from": at, "count": asked})
    chunks = []
    while len(chunks) < asked:
        frame = refuse_if_nacked(await link.frame(args.timeout))
        if frame.get("cmd") != "chunk":
            raise SystemExit("unexpected reply to read: %s" % json.dumps(frame))
        chunks.append(frame)
        if frame.get("eof"):
            break
    return chunks


def chunks_per_read(window):
    """The device's own clamp, so the host waits for the frames this read brings and no others."""
    if window <= 0:
        return 1
    return min(window, LOG_READ_CHUNKS_MAX)


async def list_sessions(link, args):
    await link.write({"cmd": "list", "log": args.log})
    count = refuse_if_nacked(await link.frame(args.timeout))
    sessions = []
    for index in range(count.get("sessions", 0)):
        await link.write({"cmd": "list", "log": args.log, "index": index})
        sessions.append(refuse_if_nacked(await link.frame(args.timeout)))
    if count.get("truncated"):
        print("the device holds more sessions than its index offers")
    return sessions


def refuse_if_nacked(frame):
    if frame.get("ack") is False:
        raise SystemExit("device refused: %s" % frame.get("reason", "no reason given"))
    return frame


def nmea_checksum(body):
    total = 0
    for char in body.encode():
        total ^= char
    return "%02X" % total


def nmea_checksum_ok(line):
    if not line.startswith("$") or "*" not in line:
        return False
    body, _, given = line[1:].partition("*")
    return nmea_checksum(body) == given.strip().upper()


class LineAssembler:
    """A notification is a slice of a byte stream, not a sentence."""

    def __init__(self):
        self.held = ""

    def feed(self, data):
        self.held += data.decode("utf-8", errors="replace")
        parts = self.held.replace("\r\n", "\n").split("\n")
        self.held = parts.pop()
        return [line for line in parts if line]


def host_time(monotonic_at):
    wall = time.time() - (time.monotonic() - monotonic_at)
    return "%s.%03d" % (time.strftime("%H:%M:%S", time.localtime(wall)), wall % 1 * 1000)


def bleak_scanner():
    """Imported on demand so the decoders and the tests run without bleak installed."""
    from bleak import BleakScanner

    return BleakScanner


async def connect(args, stream=False, config=False, log=False):
    from bleak import BleakClient

    device = args.address or await find(args)
    client = BleakClient(device)
    began = time.monotonic()
    await client.connect()
    link = Link(client, (time.monotonic() - began) * 1000)
    await link.open(stream=stream, config=config, log=log)
    return link


async def find(args):
    seen = await bleak_scanner().discover(timeout=args.scan_seconds, return_adv=True)
    hits = [pair for pair in seen.values()
            if pair[1].local_name and pair[1].local_name.startswith(args.name)]
    if not hits:
        raise SystemExit("no device advertising a name starting with %r" % args.name)
    device, advert = max(hits, key=lambda pair: pair[1].rssi or -999)
    print("# %s %s at %s dBm" % (advert.local_name, device.address, advert.rssi), file=sys.stderr)
    return device


class Link:
    """One connected device: the NMEA stream, the config endpoint and the log endpoint."""

    def __init__(self, client, connect_ms):
        self.client = client
        self.connect_ms = connect_ms
        self.lines = asyncio.Queue()
        self.replies = asyncio.Queue()
        self.frames = asyncio.Queue()
        self.notifications = []
        self.assembler = LineAssembler()

    @property
    def address(self):
        return self.client.address

    def mtu(self):
        try:
            return self.client.mtu_size
        except Exception as error:
            return "unavailable (%s)" % type(error).__name__

    def payload_bytes(self):
        mtu = self.mtu()
        return mtu - 3 if isinstance(mtu, int) else mtu

    async def open(self, stream=False, config=False, log=False):
        if stream:
            await self.client.start_notify(NUS_TX, self.on_stream)
        if config:
            await self.client.start_notify(CONFIG_CHAR, self.on_config)
        if log:
            await self.client.start_notify(LOG_CHAR, self.on_log)

    def on_stream(self, _, data):
        at = time.monotonic()
        self.notifications.append((at, len(data)))
        for line in self.assembler.feed(bytes(data)):
            self.lines.put_nowait((at, line))

    def on_config(self, _, data):
        self.replies.put_nowait((time.monotonic(), bytes(data).decode(errors="replace")))

    def on_log(self, _, data):
        self.frames.put_nowait(bytes(data).decode(errors="replace"))

    async def command(self, body, timeout):
        """Every frame of one config reply, ended by its own more flag and not by a count."""
        sent = time.monotonic()
        await self.client.write_gatt_char(CONFIG_CHAR, json.dumps(body).encode(), response=True)
        out = []
        while True:
            at, text = await asyncio.wait_for(self.replies.get(), timeout)
            out.append((at - sent, text))
            if not more_follows(text):
                return out

    async def write(self, body):
        await self.client.write_gatt_char(LOG_CHAR, json.dumps(body).encode(), response=True)

    async def frame(self, timeout):
        try:
            return json.loads(await asyncio.wait_for(self.frames.get(), timeout))
        except asyncio.TimeoutError:
            raise SystemExit("no reply on the log endpoint within %.1f s" % timeout)

    def forget_stream(self):
        while not self.lines.empty():
            self.lines.get_nowait()
        self.notifications.clear()

    async def stream_lines(self, deadline):
        while True:
            left = deadline - time.monotonic()
            if left <= 0:
                return
            try:
                yield await asyncio.wait_for(self.lines.get(), left)
            except asyncio.TimeoutError:
                return

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_):
        await self.client.disconnect()


def more_follows(text):
    try:
        return bool(json.loads(text).get("more"))
    except (json.JSONDecodeError, AttributeError):
        return False


if __name__ == "__main__":
    sys.exit(main())
