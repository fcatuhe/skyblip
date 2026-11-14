# The ADS-L conformance suite, the device's half

ADS-L 4 SRD-860 issue 2, read clause by clause against this firmware. The normative text is [`docs/reference/ads-l_4_srd860_issue_2.md`](../../../docs/reference/ads-l_4_srd860_issue_2.md), and the PDF beside it is what wins where the two disagree.

The suite has two halves. The cases the ADS-L component satisfies on its own travel with it, in [`protocols/adsl/test/conformance/`](../../protocols/adsl/test/conformance), and build with nothing from skyBlip. The cases here need skyBlip around the component: the simulator, a product, the dwell map, the own-ship encoder or the radio part. Together they are the whole reading, and nothing else in the tree is named after a clause. The suites under `test/core/` and the component's own `test/` test the code (`protocols/adsl/test/test_packet.cpp` takes the packet over a bit-error channel, `test/core/test_timing.cpp` the dwell map); these test the document.

## One case per clause

A case is named `ADS-L.4.SRD860.<clause>: what holds`, so the subject is the clause and `docs/BEHAVIOR.md` groups by it. `scripts/check_adsl_clauses.py` is the gate over both halves: every `ADS-L.4.SRD860.x.y` heading in the specification has at least one case in one of them, and every case names a real clause. Each half names a file after the subpart it reads, and has no file for a subpart it has no case in:

| File | Clauses | Here, in the device's half | In the component's half |
|---|---|---|---|
| `test_general.cpp` | A.1 to A.5, and the performance appendix | the manufacturer's paperwork and the transmitter's power and latency | what the specification is for and covers |
| `test_overview.cpp` | B.1 to B.6 | soft versioning as flown: the version own Traffic goes out in beside a close neighbour, and a Status filed against its sender | bit order, the transmission sequence, reserved bits, B.5's close-client test and the version a packet declares |
| `test_physical.cpp` | C.1 to C.5 | the radio's preamble and power, channel alternation as flown, the time multiplexing the dwell map does | bands, modulation, Manchester coding, sync words |
| `test_datalink.cpp` | D.1 to D.3 | listen-before-talk as flown: the deferral, the 3000 ms force on the ground and the 2000 ms after it, and no force airborne | the whole subpart, `media_access()` among it |
| `test_network.cpp` | E.1 to E.4.2 | the version own-ship's Traffic goes out in | the whole subpart |
| `test_presentation.cpp` | F.1 to F.2.5 | payload 66, skyBlip's address table, the ICAO address F.2.3 puts in its place, and the configurations it refuses | the header and the payload types |
| `test_traffic.cpp` | G.1 to G.1.10, G.1.16 | what own-ship becomes on air: invalid fields, the instant, the flight state, the category | every field's offset and code |
| `test_quality.cpp` | G.1.11 to G.1.17 | the integrity and accuracy own-ship claims | the code tables |
| `test_payloads.cpp` | G.2 to G.5 | the Status a Go flies and the uplink it hears | the Status payload's layout and cadence, and the payloads not implemented |

## A gap is a skipped case, never a missing one

Where the firmware does not satisfy a clause, the case is still written as the clause requires, marked `doctest::skip()` over a one-line `TODO:` naming why. `build/skyblip_tests --no-skip --test-case="ADS-L*"` runs them all and prints what is not conformant, which is the list of what this device would have to do to claim the clause. Four kinds of skip live in the two halves, told apart by how the message opens, and `scripts/check_adsl_clauses.py` refuses a skip that opens on none of them:

- `not implemented:` a mandatory clause the firmware does not meet. There is none today. F.2.3's configured ICAO address was the last, and its cases now pass. They test our reading, that every payload goes under table 5 while an ICAO address is set, which EASA is being asked to confirm (`core/settings/README.md`, The ICAO address).
- `prose:` the clause has nothing executable, such as the block diagram or the OSI table. G.5 is one too: it reserves a payload for drones and leaves its format to a later issue.
- `paperwork:` a duty of the manufacturer rather than of the device, such as the declaration of conformity.
- `optional:` a clause the specification leaves optional, because its wording says "may", "optionally" or "should", or because the Status payload lets a device declare the capability absent. These are the ones this device does not take:

| Clause | What it is | The wording that makes it optional | What our Status declares |
|---|---|---|---|
| C.3, C.3.1, C.3.2, D.1.2 | the O-band LDR channel at 38.4 kbps, its sync word, preamble and legacy packet | G.2.3 gives O-band LDR receive a None class, so a device can declare the channel absent | O-band LDR receive 0, None |
| E.4, E.4.1, E.4.2 | the Ed25519 secure signature and its timestamp | E.4: "ADS-L clients may optionally digitally sign the transmitted data packets". E.1 calls it "the optional secure signature" | no Status field. Every packet sets E.1.2's Secure Signature flag to 0 |
| F.2.4 | privacy mode, AMT 0 with an address drawn at start-up | F.2.4: "a device may be operated in privacy mode" | no Status field |
| G.4 | the FIS-B Uplink payload, DO-358B's ground uplink message | G.2.10 lets a sender declare "Sender does not process FIS-B Uplink payloads" | FIS-B client 0 |

Skipped cases carry no claim, so they never reach `docs/BEHAVIOR.md`: the generated index stays a list of what the suite checks.

## EN 300 220

D.3 asks for listen-before-talk according to ETSI EN 300 220-1, so the clauses of that standard the device answers to have a suite of their own, [`test/en300220/`](../en300220/README.md), named and checked the same way.

## Where the oracles come from

Where the specification prints code or a worked example, the test carries it rather than restating our answer, and those cases are the component's: `protocols/adsl/README.md` lists them.
