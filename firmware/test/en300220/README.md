# The EN 300 220 suite

ETSI EN 300 220-1 V3.1.1 and EN 300 220-2 V3.3.1, read clause by clause against this device, for the clauses its 868 MHz transmitter answers to: band M's row of -2 Table 4, the duty cycle it declares, and the polite spectrum access ADS-L 4 SRD-860 §D.3 asks for by name. The ADS-L suite (`test/adsl/README.md`) reads §D.3's own two numbers. This one reads the standard §D.3 points to.

## One case per clause

A case is named `EN 300 220-<part> <clause>: what holds`, as in `EN 300 220-1 5.21.2.2: the CCA threshold is 15 dB over the sensitivity limit, at the front end`, so `docs/BEHAVIOR.md` groups by the clause. `test_part1.cpp` reads EN 300 220-1 and `test_part2.cpp` EN 300 220-2.

ETSI does not let the standards be committed (`docs/reference/README.md`), so CI cannot read their headings the way `scripts/check_adsl_clauses.py` reads ADS-L's. [`clauses.txt`](clauses.txt) stands in: one line per clause the suite reads, its number and its heading, and no other text of the standard. `scripts/check_en300220_clauses.py` fails when a listed clause has no case, when a case names a clause the list does not, and when a skipped case opens on none of the ADS-L suite's four kinds. Where `scripts/fetch_etsi.py` has fetched the standards, it also checks each listed heading against the document, so a typo in the list cannot stand in for a clause. CI runs it, and `scripts/test_check_en300220_clauses.py` checks the checker.

A gap is a skipped case, written as the clause requires, as in the ADS-L suite. The two skipped today are `optional:`: adaptive frequency agility (-1 5.21.4.1, -2 4.6.1.1), which the standard offers as an alternative to waiting, and which ADS-L's §C.2.5 rules out by giving each second its channel.

## What it holds the device to

| Clause | What the case checks |
|---|---|
| -1 5.21.1 | the executor keys below the CCA threshold and reports the burst busy above it, and a deferral is a random whole number of 5 ms units |
| -1 5.21.2.2 | the threshold is 15 dB over Table 32's limit for a 200 kHz receiver, corrected by the antenna's gain as Table 45's example says, then less the feed's loss to the chip, and the executor keys 1 dB under it and holds at it |
| -1 5.21.3.1 | the CCA interval, the dead time, Ton, the hour's cumulative on-time, the deferral floor, and 100 ms between two of our bursts on one frequency over 21 s of flight |
| -1 5.21.3.2 | the seven declarations `core/timing/README.md` tabulates, at their values |
| -1 5.21.4.1 | skipped, `optional:` |
| -2 4.2.4 | both channels sit inside band M, and the e.r.p. is under 25 mW |
| -2 4.4.3.3 | the hour's air time stops at 1 %, and the transmitter refuses a burst past it |
| -2 4.6.1.0 | a held channel defers a burst in the simulator, and the burst goes once the channel clears |
| -2 4.6.1.1 | skipped, `optional:` |
| -2 4.6.2.3 | the threshold at the antenna connector is Table 17's limit corrected by this antenna's gain |
| -2 4.6.3.2 | the timing parameters are inside Table 18 |

The cases compare against the standard's numbers written as literals, never against `protocols/common/en300220.h`, so a wrong figure there fails here.
