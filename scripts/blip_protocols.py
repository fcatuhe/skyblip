"""blip.py's protocols subcommand: the radio protocol versions a unit carries.

    ./scripts/blip.py protocols

The unit names the version of each radio protocol stack it carries, one line
each. What each version implements is in the release's PROTOCOLS.md.
"""
import blip_frames as frames

NAMES = {"adsb": "ADS-B", "adsl": "ADS-L", "alptas": "ALP-TAS"}
NONE_NAMED = "the unit named no radio protocol"


def add_parser(sub, run):
    protocols = sub.add_parser("protocols", help="the radio protocol versions the unit carries")
    protocols.add_argument("--timeout", type=float, default=4.0)
    protocols.set_defaults(run=run)


async def answer(link, timeout, decode=frames.decode_frame):
    """The unit's protocols frame or its refusal."""
    await link.write_config({"command": "protocols"})
    return await frames.next_frame(link, ("protocols", "acknowledgement"), timeout, "answer", decode)


def lines(frame, refuse):
    """One line per stack the unit named, as the device page says it: ADS-L 0.1.0."""
    refuse(frame)
    named = ["%s %s" % (name, frame[stem + "_version"])
             for stem, name in NAMES.items() if isinstance(frame.get(stem + "_version"), str)]
    return named or [NONE_NAMED]
