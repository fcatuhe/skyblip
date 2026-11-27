# The DO-260B conformance suite, the device's half

This suite reads RTCA DO-260B, draft version 4.2 (working paper 1090-WP30-18), clause by clause against Bravo's receiver. RTCA holds its copyright, so `scripts/fetch_rtca.py` fetches it and [`clauses.txt`](clauses.txt) stands in for its headings. The cases the ADS-B stack satisfies alone travel with it in [`protocols/adsb/test/conformance/`](../../protocols/adsb/test/conformance). The cases here, in `test_receiver.cpp`, need Bravo's LR2021, clock and driver around it.

## Which clauses are read

Bravo only receives, so `clauses.txt` holds the 282 receive-side headings: the frame and the messages Bravo decodes (2.2.3.2), the top heading of each it does not, the receiver not shared with TCAS (2.2.4), the message processors, reports and assembly (2.2.6 to 2.2.10), TIS-B and ADS-R processing (2.2.17.4, 2.2.18.4, 2.2.18.5), and CPR (A.1.7). Bravo answers to equipage class A0, so a clause that binds only classes A1S to A3 is `optional:`.

A case is named `DO-260B <clause>: what holds`, and `scripts/check_clauses.py` holds the cases to the list. `python3 scripts/conformance.py adsb` generates [`docs/ADSB_CONFORMANCE.md`](../../../docs/ADSB_CONFORMANCE.md).

## A gap is a skipped case or a marked clause

A clause the firmware does not meet is written as the clause requires and skipped over a `TODO:`, when a case can state it with what the stack decodes today. A clause no case can state yet, such as prose, a byte layout or a field the decoder never reads, is marked in `clauses.txt` under its heading: the claim on a line opening `  - `, then the reason indented four spaces, with any `TODO:` above it. Both take the ADS-L suite's kinds and one more, `unmeasured:`, for a radio figure no bench has measured yet, such as class A0's -72 dBm MTL (#172). `optional:` also covers a byte layout bound only to equipment using the sample report structure. `build/skyblip_tests --no-skip --test-case="DO-260B*"` runs the skipped cases. A mark goes when a case reads its clause.

## What Bravo does not meet

- **Fields read and not reported.** The surveillance status, the intent change flag, every integrity and accuracy figure but GVA, the capability class, the operational mode, the length and width code, the track/heading bit and the HRD.
- **Messages not decoded.** TYPE 28, the emergency/priority status, and TYPE 29, the target state and status.
- **DF18.** The decoder refuses it, and the receiver thread's screen drops it before the queue, so Bravo hears no TIS-B and no ADS-R.
- **The radio.** The LR2021's OOK threshold is the part's own fixed -106 dBm, so there is no DMTL (2.2.4.3.3, 2.2.4.3.4.1 to 2.2.4.3.4.3). The part reports no pulse edges (2.2.4.3.4.7.1).
- **Error correction.** The LR2021 hands up no doubtful bit, so Bravo applies only a one- or two-bit repair of a DF17 by its syndrome, never in the DF field, kept for an aircraft already held (2.2.4.3.4.7.3.a). It cannot refuse a run of doubtful bits (2.2.4.3.4.7.3) or apply the rest of the correction 2.2.4.5 names for class A0.
- **Movement codes 2 to 8.** `kMovementBands` follows Doc 9871's 0.125 kt steps, which differ from Table 2-18 by less than 0.13 kt.
- **Reports.** Nothing is estimated (2.2.8.1.4.1, 2.2.8.1.17, 2.2.8.1.18). A T 1 position keeps the instant it was received, not its 0.2 s UTC epoch. The `Track` does not carry the call sign. A surface message with a valid movement and no valid heading reports no ground speed.
- **Track lifetimes.** The traffic table forgets a target after 6 missed reports, 6 s airborne and 60 s on the ground (`core/traffic/lease.h`), against 25 +-5 s and 200 +-5 s (2.2.10.4.1.3, 2.2.10.4.2.3). The owner keeps them for now.
- **Duplicate addresses.** A second aircraft sending a tracked address only counts as implausible. No candidate record is opened and no duplicate flag is reported (2.2.10.7).

## Where the oracles come from

The cases carry the draft's figures as literals. The Gillham altitudes are encoded in the case by Annex 10's Gray steps, and CPR by `squitter.h`'s floating-point copy of A.1.7.3, apart from the integer decoder.
