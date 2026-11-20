"""blip.py's capture subcommand: arm or stop a unit's diagnostics capture.

    ./scripts/blip.py --name "skyBlip Bravo" capture power_run
    ./scripts/blip.py --name "skyBlip Go" capture stop

It waits for the press that answers the unit's prompt for as long as the prompt stands.
"""
import blip_frames as frames

PROFILES = tuple(value["name"] for value in frames.REGISTRY["enums"]["capture_profile"]["values"])
# INFO: fc 12nov26 comms::kPromptCeilingMs is 30 s, and the expired verdict lands in the pass after it
VERDICT_WAIT_S = 35.0
PRESS_TO_ALLOW = ("the unit asks on its glass, or a Bravo blinks its lamp green fast: "
                  "press the button to allow it")


def add_parser(sub, run):
    capture = sub.add_parser("capture", help="arm or stop a unit's diagnostics capture, "
                                             "allowed with a press on the unit")
    capture.add_argument("profile", choices=PROFILES, help="the capture to arm, or stop")
    capture.add_argument("--timeout", type=float, default=VERDICT_WAIT_S)
    capture.set_defaults(run=run)


async def run(link, profile, timeout, say=print, decode=frames.decode_frame):
    await link.write_config({"command": "capture", "profile": profile})
    frame = await next_acknowledgement(link, timeout, decode)
    if frame.get("pending"):
        say(PRESS_TO_ALLOW)
        frame = await press_verdict(link, timeout, decode)
    return verdict(frame, profile)


async def next_acknowledgement(link, timeout, decode):
    return await frames.next_frame(link, ("acknowledgement",), timeout, "verdict", decode)


async def press_verdict(link, timeout, decode):
    # INFO: fc 20nov26 the link can deliver a pending acknowledgement twice, so wait past every one
    while True:
        frame = await next_acknowledgement(link, timeout, decode)
        if not frame.get("pending"):
            return frame


def verdict(frame, profile):
    if frame.get("acknowledge") is True and frame.get("reason") == "capture":
        return "capture stopped" if profile == "stop" else "%s capture armed" % profile
    raise SystemExit("device refused: %s" % frame.get("reason", "no reason given"))
