#!/usr/bin/env python3
"""Download the DO-260B draft the ADS-B stack is read against, and convert it to text.

    python3 scripts/fetch_rtca.py

RTCA holds the copyright, so the PDF and its text are git-ignored (docs/reference/.gitignore)
and fetched on demand, as fetch_etsi.py does for ETSI. The draft is the latest free text of
DO-260B: working paper 1090-WP30-18, draft version 4.2. Needs pdftotext (poppler-utils).
"""
import pathlib
import subprocess

from fetch_etsi import REFERENCE, download, verify

DOCUMENT = {
    "name": "rtca_do-260b_draft_v4.2",
    "url": "https://www.anteni.net/adsb/Doc/1090-WP30-18-DRAFT_DO-260B-V42.pdf",
    "sha256": "bbb44e33359f1f03053d8733776f4dc8b922b2a60de824fec80b9869502247cf",
}


def main():
    pdf = pdf_path()
    data = download(DOCUMENT["url"])
    verify(data, DOCUMENT["sha256"], DOCUMENT["url"])
    pdf.write_bytes(data)
    subprocess.run(["pdftotext", "-layout", str(pdf), str(text_path())], check=True)
    print(f"{pdf.name} and {text_path().name} written")


def pdf_path():
    return REFERENCE / f"{DOCUMENT['name']}.pdf"


def text_path():
    return REFERENCE / f"{DOCUMENT['name']}.txt"


if __name__ == "__main__":
    main()
