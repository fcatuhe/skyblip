#!/usr/bin/env python3
"""Download the ETSI standards kept beside the ADS-L specs and convert them.

    python3 scripts/fetch_etsi.py

ETSI's copyright notice forbids redistribution, so the PDFs and their markdown
conversions are git-ignored (docs/reference/.gitignore) and fetched on demand.
Needs pdftotext (poppler-utils).
"""
import hashlib
import pathlib
import subprocess
import sys
import tempfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
REFERENCE = ROOT / "docs" / "reference"
USER_AGENT = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/124 Safari/537.36"
BASE = "https://www.etsi.org/deliver/etsi_en/300200_300299"

DOCUMENTS = [
    {
        "name": "etsi_en_300_220-1_v3.1.1",
        "url": f"{BASE}/30022001/03.01.01_60/en_30022001v030101p.pdf",
        "sha256": "42e0b9924e63c64f4e0606b716635946e2b36f7c5ced5dbf128222d02c022178",
    },
    {
        "name": "etsi_en_300_220-2_v3.3.1",
        "url": f"{BASE}/30022002/03.03.01_60/en_30022002v030301p.pdf",
        "sha256": "f7b46ac8b50e2befd976dc5fd76d2994c3f371a97f7b24303f6769deec064373",
    },
]


def main():
    for document in DOCUMENTS:
        fetch(document)


def fetch(document):
    pdf = pdf_path(document)
    data = download(document["url"])
    verify(data, document["sha256"], document["url"])
    pdf.write_bytes(data)
    convert(pdf, md_path(document))
    print(f"{pdf.name} and {md_path(document).name} written")


def pdf_path(document):
    return REFERENCE / f"{document['name']}.pdf"


def md_path(document):
    return REFERENCE / f"{document['name']}.md"


def download(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def verify(data, expected, url):
    if not data.startswith(b"%PDF"):
        raise ValueError(f"{url} did not return a PDF (a challenge page?)")
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise ValueError(f"{url} has SHA-256 {actual}, expected {expected}")


def convert(pdf, md):
    with tempfile.TemporaryDirectory() as tmp:
        text = pathlib.Path(tmp) / "spec.txt"
        subprocess.run(["pdftotext", "-layout", str(pdf), str(text)], check=True)
        subprocess.run([sys.executable, str(ROOT / "scripts" / "etsi_to_md.py"), str(text), str(md)], check=True)


if __name__ == "__main__":
    main()
