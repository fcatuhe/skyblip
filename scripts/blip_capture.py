"""blip.py's capture subcommand: arm or stop a skyBlip Bravo's diagnostics capture.

    ./scripts/blip.py --name "skyBlip Bravo" capture power_run
    ./scripts/blip.py --name "skyBlip Bravo" capture stop

The unit blinks its prompt on the lamp and waits for two presses on its button,
so this waits for that verdict as long as the prompt stands. A Go answers
unknown_command: its captures are armed on its glass only.
"""
import asyncio

import blip_frames as frames

PROFILES = tuple(value["name"] for value in frames.REGISTRY["enums"]["capture_profile"]["values"])
# INFO: fc 12nov26 comms::kPromptCeilingMs is 30 s, and the expired verdict lands in the pass after it
VERDICT_WAIT_S = 35.0
PRESS_TWICE = "the lamp blinks green fast: press the button twice to allow it, once to refuse it"


def add_parser(sub, run):
    capture = sub.add_parser("capture", help="arm or stop a Bravo's diagnostics capture, "
                                             "allowed with two presses on the unit")
    capture.add_argument("profile", choices=PROFILES, help="the capture to arm, or stop")
    capture.add_argument("--timeout", type=float, default=VERDICT_WAIT_S)
    capture.set_defaults(run=run)


async def run(link, profile, timeout, say=print):
    """The unit's verdict on one capture command, said as it comes."""
    replies = await link.command({"command": "capture", "profile": profile}, timeout)
    frame = replies[-1][1]
    if frame.get("pending"):
        say(PRESS_TWICE)
        frame = await next_reply(link, timeout)
    return verdict(frame, profile)


async def next_reply(link, timeout):
    try:
        _, data = await asyncio.wait_for(link.replies.get(), timeout)
    except asyncio.TimeoutError:
        raise SystemExit("no verdict from the unit within %.0f s" % timeout)
    return frames.decode_frame(data)


def verdict(frame, profile):
    if frame.get("acknowledge") is True and frame.get("reason") == "capture":
        return "capture stopped" if profile == "stop" else "%s capture armed" % profile
    raise SystemExit("device refused: %s" % frame.get("reason", "no reason given"))
