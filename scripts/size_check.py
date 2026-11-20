#!/usr/bin/env python3
"""Fail if an image outgrows the flash its slot leaves it, or the RAM its board has.

    python3 scripts/size_check.py <zephyr.elf>

The budgets are read off the .config beside the ELF, as the linker derives its regions:
flash is slot0 less CONFIG_ROM_END_OFFSET, the trailer and TLVs MCUboot reserves, and RAM
is CONFIG_SRAM_SIZE. Each figure runs from the region's start to the end of the last
segment the image places in it, so every declared stack is counted.

The linker already fails a build over either budget, so this prints the headroom. It fails
a build with no slot, which links over the factory SoftDevice and bootloader, and an ELF
with no loadable segment or one outside both regions. A swap-using-offset build must also
fit its signed image in the slot less two sectors (MCUboot swap_offset.c, app_max_size()),
or MCUboot refuses to swap it over the air.
"""
import os
import struct
import sys

ELF_HEADER_BYTES = 52
PROGRAM_HEADER_BYTES = 32
PT_LOAD = 1
# INFO: fc 15nov26 the nRF52840 page and the SPI NOR sector both slots are laid out in
SWAP_SECTOR_BYTES = 0x1000
SWAP_SPARE_SECTORS = 2
SIGNED_IMAGE = "zephyr.signed.bin"


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2])
        return 2
    elf_path = argv[1]
    config_path = os.path.join(os.path.dirname(elf_path), ".config")
    for path in (elf_path, config_path):
        if not os.path.isfile(path):
            print(f"ERROR: no such file: {path}")
            return 1
    signed_path = os.path.join(os.path.dirname(elf_path), SIGNED_IMAGE)
    signed_bytes = os.path.getsize(signed_path) if os.path.isfile(signed_path) else None
    with open(elf_path, "rb") as elf, open(config_path) as config:
        lines, found = report(elf.read(), config.read(), signed_bytes)
    for line in lines:
        print(line)
    for problem in found:
        print(f"FAIL: {elf_path}: {problem}")
    if not found:
        print("OK: within the flash and RAM budgets")
    return 1 if found else 0


def report(elf, config_text, signed_bytes=None):
    config = kconfig(config_text)
    try:
        flash_budget, flash_source = flash_budget_bytes(config)
        loads = load_segments(elf)
    except ValueError as error:
        return [], [str(error)]
    flash_start = int(config["CONFIG_FLASH_BASE_ADDRESS"], 0)
    flash_base = flash_start + int(config["CONFIG_FLASH_LOAD_OFFSET"], 0)
    flash_limit = flash_start + int(config["CONFIG_FLASH_SIZE"]) * 1024
    ram_base = int(config["CONFIG_SRAM_BASE_ADDRESS"], 0)
    ram_budget = int(config["CONFIG_SRAM_SIZE"]) * 1024
    flash_end, ram_end = flash_base, ram_base
    outside = []
    for address, load_address, file_bytes, memory_bytes in loads:
        if ram_base <= address < ram_base + ram_budget:
            ram_end = max(ram_end, address + memory_bytes)
        elif not flash_base <= address < flash_limit:
            outside.append(f"a segment at 0x{address:08x} is outside both the flash and the SRAM")
        if file_bytes:
            if flash_base <= load_address < flash_limit:
                flash_end = max(flash_end, load_address + file_bytes)
            else:
                outside.append(f"a segment loaded from 0x{load_address:08x} is outside the flash")
    if outside:
        return [], outside
    flash_used, ram_used = flash_end - flash_base, ram_end - ram_base

    lines = [f"flash: {flash_used} B used / {flash_budget} B budget "
             f"({100.0 * flash_used / flash_budget:.1f}%), {flash_source}",
             f"RAM: {ram_used} B static, stacks included / {ram_budget} B "
             f"({100.0 * ram_used / ram_budget:.1f}%)"]
    found = []
    if flash_used > flash_budget:
        found.append(f"over the flash budget by {flash_used - flash_budget} B")
    if ram_used > ram_budget:
        found.append(f"over the RAM budget by {ram_used - ram_budget} B")
    if config.get("CONFIG_MCUBOOT_BOOTLOADER_MODE_SWAP_USING_OFFSET") == "y":
        swap_line, swap_problem = swap_report(config, signed_bytes)
        lines.append(swap_line)
        found += swap_problem
    return lines, found


def swap_report(config, signed_bytes):
    if signed_bytes is None:
        return "swap: no signed image", [f"no {SIGNED_IMAGE} beside the ELF to hold to the swap limit"]
    swap_budget = int(config["CONFIG_FLASH_LOAD_SIZE"], 0) - SWAP_SPARE_SECTORS * SWAP_SECTOR_BYTES
    line = (f"swap: {signed_bytes} B signed / {swap_budget} B budget "
            f"({100.0 * signed_bytes / swap_budget:.1f}%), the slot less MCUboot's trailer and spare sectors")
    if signed_bytes > swap_budget:
        return line, [f"over the swap budget by {signed_bytes - swap_budget} B: MCUboot would refuse to swap it"]
    return line, []


def kconfig(text):
    config = {}
    for line in text.splitlines():
        name, sep, value = line.partition("=")
        if sep and name.startswith("CONFIG_"):
            config[name] = value.strip('"')
    return config


def flash_budget_bytes(config):
    slot_bytes = int(config.get("CONFIG_FLASH_LOAD_SIZE", "0"), 0)
    if not slot_bytes:
        raise ValueError(f"{config.get('CONFIG_BOARD') or 'the build'} has no slot to hold the image to")
    footer_bytes = int(config.get("CONFIG_ROM_END_OFFSET", "0"), 0)
    return slot_bytes - footer_bytes, "the slot less MCUboot's footer"


def load_segments(elf):
    if len(elf) < ELF_HEADER_BYTES or elf[:4] != b"\x7fELF" or elf[4:6] != b"\x01\x01":
        raise ValueError("not a 32-bit little-endian ELF")
    table_offset, = struct.unpack_from("<I", elf, 28)
    entry_bytes, count = struct.unpack_from("<HH", elf, 42)
    if entry_bytes < PROGRAM_HEADER_BYTES or table_offset + count * entry_bytes > len(elf):
        raise ValueError("the program header table runs past the end of the file")
    loads = []
    for index in range(count):
        kind, _, address, load_address, file_bytes, memory_bytes = struct.unpack_from(
            "<IIIIII", elf, table_offset + index * entry_bytes)
        if kind == PT_LOAD:
            loads.append((address, load_address, file_bytes, memory_bytes))
    if not loads:
        raise ValueError("no loadable segment")
    return loads


if __name__ == "__main__":
    sys.exit(main(sys.argv))
