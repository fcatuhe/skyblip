"""blip.py's protocols subcommand: the radio protocol versions a unit carries.

    ./scripts/blip.py protocols
"""
import blip_frames as frames

NAMES = {"adsb": "ADS-B", "adsl": "ADS-L", "alptas": "ALP-TAS"}
NONE_NAMED = "the unit named no radio protocol"


def add_parser(sub, run):
    protocols = sub.add_parser("protocols", help="the radio protocol versions the unit carries")
    protocols.add_argument("--timeout", type=float, default=4.0)
    protocols.set_defaults(run=run)


async def answer(link, timeout, decode=frames.decode_frame):
    await link.write_config({"command": "protocols"})
    return await frames.next_frame(link, ("protocols", "acknowledgement"), timeout, "answer", decode)


def lines(frame, refuse):
    refuse(frame)
    named = ["%s %s" % (name, frame[stem + "_version"])
             for stem, name in NAMES.items() if isinstance(frame.get(stem + "_version"), str)]
    return named or [NONE_NAMED]
