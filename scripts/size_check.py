#!/usr/bin/env python3
"""Fail if an image outgrows the flash its slot leaves it, or the RAM its board has.

    python3 scripts/size_check.py <zephyr.elf>

The budgets are the build's own, read off the .config beside the ELF, as the
linker derives its regions (Zephyr v4.4.2,
include/zephyr/arch/arm/cortex_m/scripts/linker.ld:57-64):

    flash  the code partition, slot0, less CONFIG_ROM_END_OFFSET, the image
           trailer and TLVs that MCUboot's sysbuild reserves at its end
           (MCUboot 6d3b3d2c, boot/zephyr/sysbuild/CMakeLists.txt:59-65)
    RAM    CONFIG_SRAM_SIZE, which the devicetree's zephyr,sram sets

Each figure is what the linker's memory region table prints: from the start
of the region to the end of the last segment the image places in it. Flash
holds the code, the read-only data and the initial values of data. RAM holds
data, bss and noinit, so every stack a build declares is in it: the main, ISR,
idle and workqueue stacks its Kconfig sizes and each K_THREAD_STACK_DEFINE in
noinit, and a stack member such as the radio thread's
(hardware/platform/zephyr/rf.h) in the object that holds it.

Every build fails RAM at link before it can fail here, because the linker
holds every board to CONFIG_SRAM_SIZE. A build with a slot also fails flash at
link, because the linker holds it to the same slot: for both, this prints the
headroom. Only a build with no slot can fail flash here: it links against the
whole flash, so it is held to the budget SLOTLESS_FLASH_BUDGETS names for its
board. An ELF with no loadable segment, or with one outside both the flash and
the SRAM, fails here too. The Go's budget is
its slot0 in boards/lilygo/t_echo_plus/t_echo_plus.dts less the footer its
build's CONFIG_ROM_END_OFFSET reserves.
"""
import os
import struct
import sys

ELF_HEADER_BYTES = 52
PROGRAM_HEADER_BYTES = 32
PT_LOAD = 1

T_ECHO_SLOT0_BYTES = 0xB6000
GO_MCUBOOT_FOOTER_BYTES = 0x1096
# TODO: fc 10nov26 the X1 stub has no slot, so it is held to the Go's until #170 lays out the X1's partitions
SLOTLESS_FLASH_BUDGETS = {"meshtracker_x1": T_ECHO_SLOT0_BYTES - GO_MCUBOOT_FOOTER_BYTES}


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
    with open(elf_path, "rb") as elf, open(config_path) as config:
        lines, found = report(elf.read(), config.read())
    for line in lines:
        print(line)
    for problem in found:
        print(f"FAIL: {elf_path}: {problem}")
    if not found:
        print("OK: within the flash and RAM budgets")
    return 1 if found else 0


def report(elf, config_text):
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
    return lines, found


def kconfig(text):
    config = {}
    for line in text.splitlines():
        name, sep, value = line.partition("=")
        if sep and name.startswith("CONFIG_"):
            config[name] = value.strip('"')
    return config


def flash_budget_bytes(config):
    slot_bytes = int(config.get("CONFIG_FLASH_LOAD_SIZE", "0"), 0)
    if slot_bytes:
        footer_bytes = int(config.get("CONFIG_ROM_END_OFFSET", "0"), 0)
        return slot_bytes - footer_bytes, "the slot less MCUboot's footer"
    board = config.get("CONFIG_BOARD", "")
    if board in SLOTLESS_FLASH_BUDGETS:
        return SLOTLESS_FLASH_BUDGETS[board], f"unverified: {board} has no slot yet"
    raise ValueError(f"{board or 'the build'} has no slot and no budget in SLOTLESS_FLASH_BUDGETS")


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
