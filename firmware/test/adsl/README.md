# The ADS-L conformance suite, the device's half

This suite reads ADS-L 4 SRD-860 Issue 2 clause by clause against this firmware. The normative text is [`docs/reference/ads-l_4_srd860_issue_2.md`](../../../docs/reference/ads-l_4_srd860_issue_2.md), and the PDF beside it wins where the two disagree.

The suite has two halves. The cases the ADS-L component meets on its own travel with it, in [`protocols/adsl/test/conformance/`](../../protocols/adsl/test/conformance), and build with nothing from skyBlip. The cases here need skyBlip around the component: the simulator, a product, the dwell map, the own-ship encoder or the radio part. Nothing else in the tree is named after a clause: the other suites test the code, and these two test the document.

## One case per clause

A case is named `ADS-L.4.SRD860.<clause>: what holds`, so the area's page under `docs/behavior/` groups by clause. `scripts/check_clauses.py` checks both halves: every `ADS-L.4.SRD860.x.y` heading in the specification has at least one case in one of them, and every case names a real clause. Each half names a file after the subpart it reads, and has none for a subpart it has no case in:

| File | Clauses |
|---|---|
| `test_general.cpp` | A.1 to A.5, and the performance appendix |
| `test_overview.cpp` | B.1 to B.6 |
| `test_physical.cpp` | C.1 to C.5 |
| `test_datalink.cpp` | D.1 to D.3 |
| `test_network.cpp` | E.1 to E.4.2 |
| `test_presentation.cpp` | F.1 to F.2.5 |
| `test_traffic.cpp` | G.1 to G.1.10, G.1.16 |
| `test_traffic_flown.cpp`, this half only | G.1.1 and G.1.16 over every Traffic burst a simulated Go flies |
| `test_quality.cpp` | G.1.11 to G.1.17 |
| `test_payloads.cpp` | G.2 to G.5 |

## A gap is a skipped case, never a missing one

Where the firmware does not meet a clause, the case is still written as the clause requires, marked `doctest::skip()` over a one-line `TODO:` naming why. `build/skyblip_tests --no-skip --test-case="ADS-L*"` runs them all and prints what this device would have to do to claim each clause. The message opens with one of five kinds, and `scripts/check_clauses.py` refuses a skip that opens on none of them:

- `not implemented:` a mandatory clause the firmware does not meet. There is none today.
- `prose:` the clause has nothing executable, such as the block diagram, the OSI table or G.5's reserved drone payload.
- `paperwork:` a duty of the manufacturer, not of the device, such as the declaration of conformity.
- `optional:` a clause the wording leaves optional ("may", "optionally", "should"), or a capability the Status payload lets a device declare absent. `protocols/adsl/README.md`, What is not implemented, lists the ones this device does not take.
- `unmeasured:` a figure the device must meet that no bench has measured yet. Two ADS-L cases wait on a bench to time the L76K against its PPS edge (`hardware/parts/l76k/README.md`): G.1.1's 10 ms on the receiver itself, and G.1.16 for a receiver that delivers every solution a second late, which the device cannot detect without an independent time source (`core/gnss/README.md`).

Skipped cases carry no claim, so they never reach `docs/behavior/`. `docs/ADSL_CONFORMANCE.md`, the report for the declaration of conformity, lists every clause in the specification's order with the passing and skipped cases of both halves, then the EN 300 220 cases and the stack's own tests. `python3 scripts/conformance.py adsl` generates it from the run, and CI fails when it differs from the suite.

## EN 300 220

§D.3 asks for listen-before-talk according to ETSI EN 300 220-1, so the clauses of that standard the device answers to have their own suite, [`test/en300220/`](../en300220/README.md), named and checked the same way.

## Where the oracles come from

Where the specification prints code or a worked example, the case carries it rather than restating our answer. Those cases are the component's, and `protocols/adsl/README.md` lists them.
