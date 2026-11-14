"""blip.py's protocols subcommand: the radio protocol versions a unit carries.

    ./scripts/blip.py protocols

The unit names each radio protocol stack it carries, its version and the
specification it implements, one line each. Firmware from before the protocols
frame answers unknown_command.
"""
import asyncio
import time

import blip_frames as frames

NAMES = {"adsb": "ADS-B", "adsl": "ADS-L", "alptas": "ALP-TAS"}
TOO_OLD = "this firmware does not report its radio protocols: update it to read them"


def add_parser(sub, run):
    protocols = sub.add_parser("protocols", help="the radio protocol versions the unit carries")
    protocols.add_argument("--timeout", type=float, default=4.0)
    protocols.set_defaults(run=run)


async def answer(link, timeout, decode=frames.decode_frame):
    """The unit's protocols frame or its refusal, past the frames it pushes unasked."""
    await link.write_config({"command": "protocols"})
    deadline = time.monotonic() + timeout
    while True:
        left = deadline - time.monotonic()
        try:
            _, data = await asyncio.wait_for(link.replies.get(), max(left, 0))
        except asyncio.TimeoutError:
            raise SystemExit("no answer from the unit within %.1f s" % timeout)
        frame = decode(data)
        if frame.get("command") in ("protocols", "acknowledgement"):
            return frame


def lines(frame, refuse):
    """One line per stack, as the device page says it: ADS-L 0.1.0 (ADS-L 4 SRD-860 Issue 2)."""
    if frame.get("reason") == "unknown_command":
        raise SystemExit(TOO_OLD)
    refuse(frame)
    return ["%s %s (%s)" % (name, frame[stem + "_version"], frame[stem + "_specification"])
            for stem, name in NAMES.items() if stem + "_version" in frame]
