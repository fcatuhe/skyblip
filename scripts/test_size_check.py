#!/usr/bin/env python3
"""Self-check for size_check.py, on ELF headers laid out the way the Zephyr linker writes them.

    python3 scripts/test_size_check.py
"""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import size_check  # noqa: E402

RAM = 0x20000000
GO_CONFIG = """CONFIG_BOARD="t_echo_plus"
CONFIG_FLASH_BASE_ADDRESS=0x0
CONFIG_FLASH_LOAD_OFFSET=0x34000
CONFIG_FLASH_SIZE=1024
CONFIG_FLASH_LOAD_SIZE=0xb6000
CONFIG_ROM_END_OFFSET=0x1096
CONFIG_SRAM_BASE_ADDRESS=0x20000000
CONFIG_SRAM_SIZE=256
"""
BRAVO_CONFIG = """CONFIG_BOARD="meshtracker_x1"
CONFIG_FLASH_BASE_ADDRESS=0x0
CONFIG_FLASH_LOAD_OFFSET=0x35000
CONFIG_FLASH_SIZE=1024
CONFIG_FLASH_LOAD_SIZE=0xb5000
CONFIG_ROM_END_OFFSET=0x1096
CONFIG_SRAM_BASE_ADDRESS=0x20000000
CONFIG_SRAM_SIZE=256
"""
SLOTLESS_CONFIG = """CONFIG_BOARD="nrf52840dk"
CONFIG_FLASH_BASE_ADDRESS=0x0
CONFIG_FLASH_LOAD_OFFSET=0
CONFIG_FLASH_SIZE=1024
CONFIG_FLASH_LOAD_SIZE=0
CONFIG_ROM_END_OFFSET=0
CONFIG_SRAM_BASE_ADDRESS=0x20000000
CONFIG_SRAM_SIZE=256
"""


def elf(flash_start, flash_bytes, ram_bytes):
    data_bytes = 0x100
    return image([(flash_start, flash_start, flash_bytes - data_bytes, flash_bytes - data_bytes),
                  (RAM, flash_start + flash_bytes - data_bytes, data_bytes, data_bytes),
                  (RAM + data_bytes, RAM + data_bytes, 0, ram_bytes - data_bytes)])


def image(loads):
    header = b"\x7fELF\x01\x01\x01" + bytes(9)
    header += struct.pack("<HHIIIIIHHHHHH", 2, 40, 1, 0, 52, 0, 0, 52, 32, len(loads), 40, 0, 0)
    table = b"".join(struct.pack("<IIIIIIII", size_check.PT_LOAD, 0, address, load_address,
                                 file_bytes, memory_bytes, 6, 4)
                     for address, load_address, file_bytes, memory_bytes in loads)
    return header + table


class SizeCheck(unittest.TestCase):
    def test_a_go_within_its_slot_and_its_ram_passes(self):
        lines, found = size_check.report(elf(0x34000, 529296, 153212), GO_CONFIG)
        self.assertEqual(found, [])
        self.assertEqual(lines[0], "flash: 529296 B used / 741226 B budget (71.4%), the slot less MCUboot's footer")
        self.assertEqual(lines[1], "RAM: 153212 B static, stacks included / 262144 B (58.4%)")

    def test_the_go_is_held_to_its_slot_less_the_footer(self):
        # 0xB6000 - 0x1096 = 741226, the FLASH region the Go's linker prints.
        self.assertEqual(size_check.report(elf(0x34000, 741226, 4096), GO_CONFIG)[1], [])
        self.assertEqual(size_check.report(elf(0x34000, 741227, 4096), GO_CONFIG)[1],
                         ["over the flash budget by 1 B"])

    def test_static_ram_over_the_board_fails(self):
        self.assertEqual(size_check.report(elf(0x34000, 4096, 262144), GO_CONFIG)[1], [])
        self.assertEqual(size_check.report(elf(0x34000, 4096, 262145), GO_CONFIG)[1],
                         ["over the RAM budget by 1 B"])

    def test_the_x1_is_held_to_its_own_slot_less_the_footer(self):
        # 0xB5000 - 0x1096 = 737130, the FLASH region the X1 smoke image's linker prints.
        lines, found = size_check.report(elf(0x35000, 737130, 4096), BRAVO_CONFIG)
        self.assertEqual(found, [])
        self.assertIn("737130 B budget", lines[0])
        self.assertEqual(size_check.report(elf(0x35000, 737131, 4096), BRAVO_CONFIG)[1],
                         ["over the flash budget by 1 B"])

    def test_a_board_with_no_slot_fails(self):
        self.assertEqual(size_check.report(elf(0, 4096, 4096), SLOTLESS_CONFIG)[1],
                         ["nrf52840dk has no slot to hold the image to"])

    def test_a_file_that_is_not_an_elf_fails(self):
        self.assertEqual(size_check.report(bytes(64), GO_CONFIG)[1], ["not a 32-bit little-endian ELF"])

    def test_a_segment_past_the_sram_fails_rather_than_dropping_out(self):
        loads = [(0x34000, 0x34000, 4096, 4096), (RAM + 262144, RAM + 262144, 0, 4096)]
        self.assertEqual(size_check.report(image(loads), GO_CONFIG)[1],
                         ["a segment at 0x20040000 is outside both the flash and the SRAM"])

    def test_data_loaded_from_outside_the_slot_fails(self):
        loads = [(0x34000, 0x34000, 4096, 4096), (RAM, 0x1000, 256, 256)]
        self.assertEqual(size_check.report(image(loads), GO_CONFIG)[1],
                         ["a segment loaded from 0x00001000 is outside the flash"])

    def test_an_elf_with_no_loadable_segment_exits_nonzero(self):
        code, output = run(image([]), GO_CONFIG)
        self.assertEqual(code, 1)
        self.assertIn("FAIL: ", output)
        self.assertIn("no loadable segment", output)

    def test_a_truncated_elf_exits_nonzero_without_a_traceback(self):
        code, output = run(elf(0x34000, 4096, 4096)[:60], GO_CONFIG)
        self.assertEqual(code, 1)
        self.assertIn("the program header table runs past the end of the file", output)
        self.assertNotIn("Traceback", output)

    def test_a_go_within_budget_exits_zero(self):
        code, output = run(elf(0x34000, 529296, 153212), GO_CONFIG)
        self.assertEqual(code, 0)
        self.assertIn("OK: within the flash and RAM budgets", output)


def run(elf_bytes, config_text):
    with tempfile.TemporaryDirectory() as build:
        path = pathlib.Path(build) / "zephyr.elf"
        path.write_bytes(elf_bytes)
        (pathlib.Path(build) / ".config").write_text(config_text)
        done = subprocess.run([sys.executable, size_check.__file__, str(path)],
                              capture_output=True, text=True)
    return done.returncode, done.stdout + done.stderr


if __name__ == "__main__":
    unittest.main()
