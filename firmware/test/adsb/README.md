# The DO-260B conformance suite, the device's half

RTCA DO-260B, the MOPS for 1090 MHz Extended Squitter, read clause by clause against Bravo's receiver. The text is the latest free one, working paper 1090-WP30-18, draft version 4.2 (`docs/reference/README.md`). RTCA holds its copyright, so it is fetched by `scripts/fetch_rtca.py` and never committed, and [`clauses.txt`](clauses.txt) stands in for its headings.

The suite has two halves, as the ADS-L one does (`test/adsl/README.md`). The cases the ADS-B component satisfies on its own travel with it, in [`protocols/adsb/test/conformance/`](../../protocols/adsb/test/conformance), and build with nothing from skyBlip. The cases here need Bravo around the component: the LR2021 and its model, the clock that dates a frame, and the driver that decides which downlink formats reach the decoder.

## Which clauses are read

DO-260B specifies transmitters, TCAS-shared receivers, TIS-B and ADS-R ground services and antennas, and Bravo only receives. The list holds every heading of the receive side, and no heading of the transmitter's:

- 2.2.3.2's frame and the messages Bravo decodes, field by field: the airborne and surface positions, the identification, the four velocity subtypes and the operational status. A subclause that tells a transmitter how to fill a field, such as the position extrapolation and the TYPE code a NIC selects, is left out.
- the top heading of each message Bravo does not decode: the target state and status (TYPE 29), the test message (23), the surface system status (24), the aircraft status (28) and the reserved TYPE codes.
- the receiver (2.2.4) when it is not shared with TCAS, the message processors (2.2.6, 2.2.7), the reports (2.2.8, 2.2.9) and their assembly (2.2.10), with the top heading of each on-condition report.
- the processing of TIS-B (2.2.17.4) and ADS-R (2.2.18.4, 2.2.18.5), because they reach a receiver as DF18 and Bravo drops every DF18.
- Appendix A's CPR, A.1.7.

That is 282 clauses. Bravo answers to equipage class A0, the class whose 100 track files `adsb::Assembly` keeps (Table 2-105), so a clause that binds only classes A1S to A3 is `optional:`.

| File | Half | Clauses |
|---|---|---|
| `protocols/adsb/test/conformance/test_formats.cpp` | component | 2.2.3.2 to 2.2.3.2.5: the frame, positions and identification |
| `protocols/adsb/test/conformance/test_velocity.cpp` | component | 2.2.3.2.6: the four velocity subtypes, subfield by subfield |
| `protocols/adsb/test/conformance/test_status.cpp` | component | 2.2.3.2.7: the status and event messages |
| `protocols/adsb/test/conformance/test_reception.cpp` | component | 2.2.4.3.4.5, 2.2.4.3.4.7.3, 2.2.4.4, 2.2.4.5, 2.2.6 and 2.2.7: what is accepted, refused and handed on |
| `protocols/adsb/test/conformance/test_reports.cpp` | component | 2.2.8 and 2.2.9: the reports, read as the `Track` the assembly hands out |
| `protocols/adsb/test/conformance/test_assembly.cpp` | component | 2.2.10: report assembly |
| `protocols/adsb/test/conformance/test_cpr.cpp` | component | A.1.7: compact position reporting |
| `test_receiver.cpp` | device | 2.2.4's radio, 2.2.6.1.2's delivery time, 2.2.8.5's time stamps, and DF18's TIS-B and ADS-R |

A case is named `DO-260B <clause>: what holds`. `scripts/check_clauses.py` fails when a listed clause has no case in either half, when a case names a clause the list does not, and when a skipped case opens on none of the kinds below. Where the draft is fetched, it also checks each listed heading against it, so a typo in the list cannot stand in for a clause. CI runs it, and `scripts/test_check_clauses.py` checks the checker.

[`docs/ADSB_CONFORMANCE.md`](../../../docs/ADSB_CONFORMANCE.md) puts both halves under the headings from `clauses.txt`. It lists the passing cases and the reason each skipped case makes no claim. `python3 scripts/conformance.py adsb` generates it from the host test run without the fetched draft. CI checks that it matches the suite.

## A gap is a skipped case

As in the ADS-L suite, a clause the firmware does not meet is written as the clause requires and skipped over a `TODO:`. `build/skyblip_tests --no-skip --test-case="DO-260B*"` runs them all. The kinds are the ADS-L suite's, with one more:

- `not implemented:` a clause Bravo does not meet.
- `prose:` a clause with nothing a receiver executes.
- `optional:` a clause the draft lets a class A0 receiver leave out, or a byte layout it binds only to equipment that uses its sample report structure.
- `unmeasured:` a figure the radio has to meet that no bench has measured yet, such as the -72 dBm MTL of class A0. These wait on #172.

## What Bravo does not meet

The suite found these. Each is a skipped case.

- **Fields read and not reported.** The surveillance status (an emergency, an alert, the SPI), the intent change flag, and every integrity and accuracy figure: NIC and its supplements, NACp, NACv, SIL and its supplement, SDA, NICbaro, GVA aside. Also the capability class, the operational mode, the length and width code, the track/heading bit and the HRD.
- **Messages not decoded.** TYPE 28, the emergency/priority status, the Mode A code and the TCAS RA. TYPE 29, the target state and status.
- **DF18.** The LR2021 driver hands up only frames whose first four bits are `1000`, DF16 and DF17, and the decoder reads those four bits as `1000`. So Bravo drops every DF18 and hears no TIS-B and no ADS-R.
- **The radio.** The LR2021's OOK threshold is fixed at -110 dBm, so there is no DMTL (2.2.4.3.3, 2.2.4.3.4.1 to 2.2.4.3.4.3). The part decides each bit from its chip pair and reports no pulse edges (2.2.4.3.4.7.1).
- **Error correction.** Bravo's LR2021 decides every bit itself and hands up no doubtful bit (`hardware/parts/lr2021/README.md`), so the decoder corrects nothing and refuses every frame whose parity fails. It cannot refuse a run of more than seven doubtful bits (2.2.4.3.4.7.3), and it applies none of DO-185B Appendix A §A.3's correction, which 2.2.4.5 names for class A0. Issue #373 measures on a Bravo how many refused frames a single flipped bit would save, before we decide to correct one.
- **Movement codes 2 to 8.** `kMovementBands` reads them in 0.125 kt steps from 0.125 kt, after Doc 9871 as `protocols/adsb/README.md` cites it. Table 2-18 puts code 2 under 0.125 kt and codes 3 to 8 in steps of 0.2700833 km/h above it. The two differ by less than 0.13 kt.
- **Reports.** Nothing is estimated (2.2.8.1.4.1, 2.2.8.1.17, 2.2.8.1.18). A T 1 position keeps the instant it was received, not its 0.2 s UTC epoch. The call sign stays on the `Message` and is not carried by the `Track`. A surface message with a valid movement and no valid heading reports no ground speed.
- **Track lifetimes.** The traffic table on Bravo and the Go forgets a target after 6 s airborne, whatever DO-260B's 25 ±5 s (2.2.10.4.1.3) says. On the ground it forgets it after 60 s, where 2.2.10.4.2.3 keeps the track 200 ±5 s. Both figures are 6 missed reports, for every source, in `core/traffic/lease.h`. The owner chose on 17 November 2026 to keep them for now. `adsb::Assembly` still keeps a position 120 s, after 2.2.10.3.1 f, but only for its own pairing.
- **Duplicate addresses.** A second aircraft sending a tracked address only counts as implausible. No candidate record is opened and no duplicate flag is reported (2.2.10.7).

## Where the oracles come from

The draft prints tables, not worked examples, so the cases carry its figures as literals: the CA codes of Table 2-8, the movement bands of Table 2-18, the 64 ft/min vertical rate step, the 25 ft difference step, the 360/1024 heading weight, the 10 s, 25 s and 50 s pair windows, the 6, 2.5 and 0.75 NM jumps, the 5 m and 1.25 m agreement, the 100 track files of class A0. The Gillham altitudes are encoded in the case by Annex 10's Gray steps, written apart from the decoder. CPR is encoded by `squitter.h`'s floating-point copy of A.1.7.3 and decoded by the integer stack.
