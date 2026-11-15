#!/usr/bin/env python3
"""Fail unless MCUboot embeds exactly the public keys its channel trusts.

    python3 scripts/check_bootloader_keys.py <mcuboot zephyr.bin> <public.pem>...

MCUboot holds each P-256 verification key as its DER SubjectPublicKeyInfo, and
imgtool's KEYHASH TLV is the SHA-256 of those bytes. A unit reads the same keys
off its bootloader and installs over the air only an image signed by one of
them (firmware/core/dfu/README.md). A development bootloader trusts its signing
key and firmware/keys/production.pub.pem, a release bootloader the production
key alone (firmware/keys/README.md).
"""
import base64
import hashlib
import sys

P256_KEY_PREFIX = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200")
P256_KEY_BYTES = 91


def embedded_keys(bootloader):
    found, at = set(), bootloader.find(P256_KEY_PREFIX)
    while at != -1:
        found.add(hashlib.sha256(bootloader[at:at + P256_KEY_BYTES]).hexdigest())
        at = bootloader.find(P256_KEY_PREFIX, at + 1)
    return found


def pem_key(text):
    body = "".join(line for line in text.splitlines() if line and not line.startswith("-----"))
    return hashlib.sha256(base64.b64decode(body)).hexdigest()


def problems(bootloader, pems):
    embedded, wanted = embedded_keys(bootloader), {pem_key(pem) for pem in pems}
    found = [f"embeds key {key[:8]}, which its channel does not trust" for key in sorted(embedded - wanted)]
    found += [f"lacks key {key[:8]}" for key in sorted(wanted - embedded)]
    return found


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip().splitlines()[2])
        return 2
    with open(argv[1], "rb") as bootloader:
        image = bootloader.read()
    pems = []
    for path in argv[2:]:
        with open(path, encoding="ascii") as pem:
            pems.append(pem.read())
    found = problems(image, pems)
    for problem in found:
        print(f"FAIL: {argv[1]}: {problem}")
    if not found:
        print(f"{argv[1]}: trusts {', '.join(sorted(key[:8] for key in embedded_keys(image)))}")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
