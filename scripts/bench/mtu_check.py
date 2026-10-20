"""How each connection's ATT MTU was settled, from the down record a Full capture writes per connection.

usage: python3 scripts/bench/mtu_check.py <capture.ndjson>...

Per connection: who exchanged the MTU, what became of the unit's own request, the payload the link
settled on, how long after connecting, and how many replies it was answered small_mtu for. Then the
totals firmware/core/comms/README.md reassesses splitting frames against.
"""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import blip_records

MINIMUM_PAYLOAD_BYTES = 20


def ended_connections(records):
    return [record for record in records
            if record.get("type") == "link" and record.get("action") == "down"]


def negotiated_by(connection):
    central = connection.get("mtu_exchanged_by_central")
    unit = connection.get("mtu_exchanged_by_unit")
    if central and unit:
        return "both"
    if central:
        return "central"
    if unit:
        return "unit"
    return "none"


def request_text(connection):
    request = connection.get("mtu_request", "not_asked")
    if request in ("refused", "unsent"):
        return "%s 0x%02X" % (request, connection.get("mtu_request_error", 0))
    return request


def connection_line(connection):
    return "%-8s link %-3s %-26s %-8s %-18s %7d %8d ms %7d" % (
        "s%s" % connection.get("session", "?"), connection.get("link_session"),
        blip_records.stamp_of(connection), negotiated_by(connection), request_text(connection),
        connection.get("payload_bytes", 0), connection.get("mtu_settled_after_ms", 0),
        connection.get("replies_too_wide", 0))


def totals_line(connections):
    by = {"central": 0, "unit": 0, "both": 0, "none": 0}
    for connection in connections:
        by[negotiated_by(connection)] += 1
    narrow = sum(1 for connection in connections
                 if connection.get("payload_bytes", 0) <= MINIMUM_PAYLOAD_BYTES)
    refused = [connection.get("replies_too_wide", 0) for connection in connections]
    failed = sum(1 for connection in connections
                 if connection.get("mtu_request") in ("refused", "unsent"))
    return ("%d connections: %d exchanged by the central, %d by the unit, %d by both, %d by neither; "
            "%d unit requests failed; %d ended at %d bytes; %d replies refused small_mtu on %d connections"
            % (len(connections), by["central"], by["unit"], by["both"], by["none"], failed, narrow,
               MINIMUM_PAYLOAD_BYTES, sum(refused), sum(1 for count in refused if count)))


def report_lines(records):
    connections = ended_connections(records)
    if not connections:
        return ["no link down records: the capture was not a full profile, or nothing connected"]
    header = "%-8s %-8s %-26s %-8s %-18s %7s %11s %7s" % (
        "session", "link", "ended", "by", "unit request", "payload", "settled", "refused")
    return [header] + [connection_line(connection) for connection in connections] + [totals_line(connections)]


def main(paths):
    for path in paths:
        print("== %s" % path)
        records = [record for record in blip_records.read_ndjson(path) if not blip_records.is_note(record)]
        for line in report_lines(records):
            print("   %s" % line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
