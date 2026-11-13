# The ADS-L conformance suite, the device's half

ADS-L 4 SRD-860 issue 2, read clause by clause against this firmware. The normative text is [`docs/reference/ads-l_4_srd860_issue_2.md`](../../../docs/reference/ads-l_4_srd860_issue_2.md), and the PDF beside it is what wins where the two disagree.

The suite has two halves. The cases the ADS-L component satisfies on its own travel with it, in [`protocols/adsl/test/conformance/`](../../protocols/adsl/test/conformance), and build with nothing from skyBlip. The cases here need skyBlip around the component: the simulator, a product, the dwell map, the own-ship encoder or the radio part. Together they are the whole reading, and nothing else in the tree is named after a clause. The suites under `test/core/` and the component's own `test/` test the code (`protocols/adsl/test/test_packet.cpp` takes the packet over a bit-error channel, `test/core/test_timing.cpp` the dwell map); these test the document.

## One case per clause

A case is named `ADS-L.4.SRD860.<clause>: what holds`, so the subject is the clause and `docs/BEHAVIOR.md` groups by it. `scripts/check_adsl_clauses.py` is the gate over both halves: every `ADS-L.4.SRD860.x.y` heading in the specification has at least one case in one of them, and every case names a real clause. Each half names a file after the subpart it reads, and has no file for a subpart it has no case in:

| File | Clauses | Here, in the device's half | In the component's half |
|---|---|---|---|
| `test_general.cpp` | A.1 to A.5, and the performance appendix | the manufacturer's paperwork and the transmitter's power and latency | what the specification is for and covers |
| `test_overview.cpp` | B.1 to B.6 | soft versioning | bit order, the transmission sequence, reserved bits |
| `test_physical.cpp` | C.1 to C.5 | the radio's preamble and power, channel alternation as flown, the time multiplexing the dwell map does | bands, modulation, Manchester coding, sync words |
| `test_datalink.cpp` | D.1 to D.3 | | the whole subpart |
| `test_network.cpp` | E.1 to E.4.2 | | the whole subpart |
| `test_presentation.cpp` | F.1 to F.2.5 | payload 66, skyBlip's address table and the configuration that sets it | the header and the payload types |
| `test_traffic.cpp` | G.1 to G.1.10, G.1.16 | what own-ship becomes on air: invalid fields, the instant, the flight state, the category | every field's offset and code |
| `test_quality.cpp` | G.1.11 to G.1.17 | the integrity and accuracy own-ship claims | the code tables |
| `test_payloads.cpp` | G.2 to G.5 | the Status a Go flies and the uplink it hears | the Status payload's layout and cadence, and the payloads not implemented |

## A gap is a skipped case, never a missing one

Where the firmware does not satisfy a clause, the case is still written as the clause requires, marked `doctest::skip()` over a one-line `TODO:` naming why. `build/skyblip_tests --no-skip --test-case="ADS-L*"` runs them all and prints what is not conformant, which is the list of what this device would have to do to claim the clause. Three kinds of skip live in the two halves, told apart by the message:

- `not implemented` / a failing assertion: a real gap, the firmware could satisfy the clause and does not.
- `prose:` the clause has nothing executable, such as the block diagram or the OSI table.
- `paperwork:` a duty of the manufacturer rather than of the device, such as the declaration of conformity.

Skipped cases carry no claim, so they never reach `docs/BEHAVIOR.md`: the generated index stays a list of what the suite checks.

## Where the oracles come from

Where the specification prints code or a worked example, the test carries it rather than restating our answer, and those cases are the component's: `protocols/adsl/README.md` lists them.
