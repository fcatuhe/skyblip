# Reference specifications

Normative sources the firmware is written against, kept locally so a clause citation in `docs/BEHAVIOR.md` or a comment in `firmware/protocols/` can be checked without a network round trip.

The two ADS-L issues are published by EASA as attachments to [ED Decision 2022/024/R](https://www.easa.europa.eu/en/document-library/agency-decisions/ed-decision-2022024r). The firmware targets Issue 2; Issue 1 is here because deployed hardware and most third-party implementations predate Issue 2, and the two differ in the payload set and in Subpart C.

## ADS-L 4 SRD-860 Issue 2

The one the firmware implements. 52 pages, dated 1 December 2025. Adds the Status, Traffic Uplink, FIS-B and Remote Identification payloads, and the O-band LDR/HDR split, to Issue 1's Traffic payload.

- `ads-l_4_srd860_issue_2.pdf`, downloaded from https://www.easa.europa.eu/en/downloads/142821/en
- SHA-256 `114f45966c1b10ab0cbd22dab365a451136ff26952ce8cdcc91740666b76b2aa`
- `ads-l_4_srd860_issue_2.md` is the text conversion

## ADS-L 4 SRD-860 Issue 1

30 pages, dated 20 December 2022, titled "Technical Specification for ADS-L transmissions using SRD-860 frequency band". Its Subpart G names the position payload "iConspicuity", which Issue 2 renames "Traffic".

- `ads-l_4_srd860_issue_1.pdf`, downloaded from https://www.easa.europa.eu/en/downloads/137503/en
- SHA-256 `a547f3b83990f6f335a2c5a622a3b55b736079a6dfea340373c5cf0c8c48bd8a`
- `ads-l_4_srd860_issue_1.md` is the text conversion

## Getting the ETSI standards

The two ETSI standards below are not in the repository. ETSI's copyright notice reads: "No part may be reproduced or utilized in any form or by any means, electronic or mechanical, including photocopying and microfilm except as authorized by written permission of ETSI." The PDFs are free to download from etsi.org, so the repository holds the script that fetches them.

```
python3 scripts/fetch_etsi.py
```

The script downloads each PDF, refuses one whose SHA-256 differs from the value recorded in the script, writes it here and converts it to markdown beside it. It fails on a network error, a challenge page or a checksum mismatch. `docs/reference/.gitignore` keeps the files out of git. Code and docs may quote the short clauses they cite.

## ETSI EN 300 220-1 V3.1.1

The harmonised standard for Short Range Devices from 25 MHz to 1 000 MHz, Part 1: technical characteristics and methods of measurement. 74 pages, dated February 2017. ADS-L 4 SRD-860 §D.3 requires listen-before-talk "according to the Polite Spectrum Access rules laid out in ETSI EN 300 220-1", so clause 5.21 (CCA threshold, timing limits, adaptive frequency agility) is the text behind the firmware's channel access. It also defines the terms the other documents use: listen before talk, dead time, deferral time, cumulative on time. V3.1.1 is the latest published version.

- `etsi_en_300_220-1_v3.1.1.pdf`, fetched from https://www.etsi.org/deliver/etsi_en/300200_300299/30022001/03.01.01_60/en_30022001v030101p.pdf
- SHA-256 `42e0b9924e63c64f4e0606b716635946e2b36f7c5ced5dbf128222d02c022178`
- `etsi_en_300_220-1_v3.1.1.md` is the text conversion. Neither file is committed

## ETSI EN 300 220-2 V3.3.1

Part 2 of the same standard: the harmonised standard for access to radio spectrum for non specific radio equipment up to 500 mW e.r.p. Its Table 4 gives the band M (868.0 MHz to 868.6 MHz) entry the firmware's duty cycle rests on: 25 mW e.r.p., "≤ 1 % duty cycle or polite spectrum access". Clause 4.6 repeats the polite spectrum access limits for the bands where it replaces the duty cycle. 107 pages, dated March 2025. V3.3.1 is the latest published version and the one the code cites.

- `etsi_en_300_220-2_v3.3.1.pdf`, fetched from https://www.etsi.org/deliver/etsi_en/300200_300299/30022002/03.03.01_60/en_30022002v030301p.pdf
- SHA-256 `f7b46ac8b50e2befd976dc5fd76d2994c3f371a97f7b24303f6769deec064373`
- `etsi_en_300_220-2_v3.3.1.md` is the text conversion. Neither file is committed

## The conformance suite

Issue 2 is read clause by clause by [`firmware/protocols/adsl/test/conformance/`](../../firmware/protocols/adsl/test/conformance), the cases the ADS-L component satisfies alone, and [`firmware/test/adsl/`](../../firmware/test/adsl), the cases that need the device around it, one file per subpart, every case named after the clause it checks: `ADS-L.4.SRD860.G.1.7: the altitude encodes the clause's worked examples`. Every heading of the document has a case, including the clauses this firmware does not satisfy, which are written as the clause requires and skipped over a `TODO:` naming the gap.

`scripts/check_adsl_clauses.py` is what keeps the two lists equal: it reads the clause headings out of the markdown beside this file and the case subjects out of the suite, and fails on either a clause nobody read or a case naming a clause that does not exist. The suite's own README says how a gap is written.

The EN 300 220 clauses the device answers to have a suite of their own, [`firmware/test/en300220/`](../../firmware/test/en300220), named `EN 300 220-1 5.21.1: what holds`. Since the standards are not committed, `firmware/test/en300220/clauses.txt` lists the clauses it reads by number and heading, and `scripts/check_en300220_clauses.py` holds the suite to that list, and the list to the fetched documents' headings when they are here.

## The conversions

Each `.md` is generated, for grepping and quoting, never edited by hand:

```
pdftotext -layout docs/reference/ads-l_4_srd860_issue_2.pdf /tmp/spec.txt
python3 scripts/spec_to_md.py /tmp/spec.txt docs/reference/ads-l_4_srd860_issue_2.md
```

The ETSI documents use `scripts/etsi_to_md.py` instead (`scripts/fetch_etsi.py` runs it), which does the same for ETSI's layout: it drops the running header and the lone `ETSI` footer, and promotes numbered clauses and `Annex` titles to headings by depth.

```
pdftotext -layout docs/reference/etsi_en_300_220-1_v3.1.1.pdf /tmp/etsi.txt
python3 scripts/etsi_to_md.py /tmp/etsi.txt docs/reference/etsi_en_300_220-1_v3.1.1.md
```

`scripts/spec_to_md.py` drops the ADS-L running header and footer and promotes `SUBPART` and `ADS-L.4.SRD860.x.y` lines to headings. The rest is `scripts/layout_to_md.py`, which both converters share. It drops a header or footer only at a page's top or bottom edge, so a line of the body that reads the same stays. It runs the pages on into each other, so a sentence or a table that crosses a page break stays whole. It moves each footnote from the foot of its page to the end of the clause in which that page ends, as a paragraph. It drops the table of contents from its first dotted entry to its last, keeping the text after it. It joins wrapped lines into paragraphs, and a word hyphenated over a line break, as `ADS-` and `L.4.SRD860.E.3.2`, back into one word. It fences every column-aligned block verbatim. Tables keep the PDF's spacing inside those fences rather than becoming markdown tables: the field-offset tables lose their alignment otherwise. Figures and block diagrams are not carried over.

`scripts/check_spec_markdown.py` checks every conversion against its PDF, and CI runs it on the two ADS-L issues. It reads each PDF again with `pdftotext -raw`, the PDF's own reading order, and requires every page to hold the same characters as the layout extraction, so the layout extraction lost nothing. It then compares the layout extraction with the markdown word for word, in order, and reports every run of words found in one and not the other, with the clause it falls in. It also reports a wrapped line or a table split into two markdown blocks, and a footnote that is missing or does not close the clause in which its page ends. Before comparing, it drops the header and footer at each page's edges, the footnote separator and the table of contents, ignores heading marks and code fences, and reads every dash as `-`. The ETSI documents are not committed, so CI cannot check them: the script checks them when `scripts/fetch_etsi.py` has fetched them, and says it skipped them otherwise.

```
python3 scripts/check_spec_markdown.py
```

The PDF is the normative text. Where the two disagree, the PDF wins.

## Copyright

The ETSI standards are copyright of ETSI, not skyBlip's, and are not redistributed here. See "Getting the ETSI standards" above.

The two ADS-L issues and their conversions are copyright of the European Union Aviation Safety Agency, not skyBlip's, and the repository's MIT license does not cover them. They are reproduced under EASA's general permission: "Reproduction is authorised, provided the source is acknowledged, save where otherwise stated" ([easa.europa.eu/en/copyright-disclaimer](https://www.easa.europa.eu/en/copyright-disclaimer)). The source is ED Decision 2022/024/R, linked above.
