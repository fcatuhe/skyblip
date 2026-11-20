# The EN 300 220 suite

This suite reads ETSI EN 300 220-1 V3.1.1 and EN 300 220-2 V3.3.1 clause by clause, for the clauses the device's 868 MHz transmitter answers to: band M's row of -2 Table 4, the duty cycle it declares, and the polite spectrum access ADS-L 4 SRD-860 §D.3 asks for by name. The ADS-L suite (`test/adsl/README.md`) reads §D.3's own two numbers, and this one the standard §D.3 points to.

## One case per clause

A case is named `EN 300 220-<part> <clause>: what holds`, as in `EN 300 220-1 5.21.2.2: the CCA threshold is 15 dB over the sensitivity limit, at the front end`, so `docs/behavior/en300220.md` groups by clause. `test_part1.cpp` reads EN 300 220-1, and `test_part2.cpp` EN 300 220-2.

ETSI does not let the standards be committed (`docs/reference/README.md`), so [`clauses.txt`](clauses.txt) stands in for their headings: one line per clause the suite reads, with its number and heading and no other text. `scripts/check_clauses.py` fails when a listed clause has no case, when a case names an unlisted clause, and when a skipped case opens on none of the ADS-L suite's kinds. Where `scripts/fetch_etsi.py` has fetched the standards, it also checks each listed heading against the document.

A gap is a skipped case, as in the ADS-L suite. The two skipped today are `optional:`: adaptive frequency agility (-1 5.21.4.1, -2 4.6.1.1), which the standard offers as an alternative to waiting, and which ADS-L's §C.2.5 rules out by giving each second its channel.

The cases compare against the standard's numbers written as literals, never against `protocols/common/en300220.h`, so a wrong figure there fails here.
