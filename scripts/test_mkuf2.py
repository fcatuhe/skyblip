#!/usr/bin/env python3
"""Self-check for mkuf2.py: the range guards, the Intel HEX round trip, and the
gate that keeps a Nordic DFU package out (check_no_dfu_package.py).

    python3 scripts/test_mkuf2.py

Runs on stdlib alone and never invokes Zephyr's uf2conv.py, so it is a host job
in CI rather than a step of the 5-minute device build. What it protects is each
board's window: below it are the factory MBR and SoftDevice, above it the
bootloader accepts writes and drops them, and past that sits the factory UF2
bootloader, which is the only way to install anything on the board without a
programmer. A partition edit is what quietly defeats those checks, so the windows
are read against each board's devicetree.
"""
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_no_dfu_package  # noqa: E402
import mkuf2  # noqa: E402

BOARDS = pathlib.Path(__file__).resolve().parent.parent / "firmware" / "boards"
BOARD_DTS = {
    "t_echo_plus": BOARDS / "lilygo" / "t_echo_plus" / "t_echo_plus.dts",
    "meshtracker_x1": BOARDS / "seeed" / "meshtracker_x1" / "meshtracker_x1.dts",
}
GO = mkuf2.WINDOWS["t_echo_plus"]
BRAVO = mkuf2.WINDOWS["meshtracker_x1"]


def span(start, length, fill=0xA5):
    return {start + i: fill for i in range(length)}


def hex_file(byte_map, directory, name="in.hex"):
    path = pathlib.Path(directory) / name
    mkuf2.write_hex(byte_map, path)
    return path


def internal_partitions(board):
    text = BOARD_DTS[board].read_text()
    flash0 = text[text.index("&flash0 {"):]
    flash0 = flash0[:flash0.index("\n};")]
    found = re.findall(r"(\w+): partition@\w+ \{[^}]*?reg = <(0x\w+) (0x\w+)>", flash0)
    return {label: (int(start, 16), int(start, 16) + int(size, 16)) for label, start, size in found}


class WindowsAgainstTheDevicetree(unittest.TestCase):
    def test_every_board_has_a_window_and_a_devicetree(self):
        self.assertEqual(set(mkuf2.WINDOWS), set(BOARD_DTS))

    def test_each_window_is_its_board_partition_map(self):
        for board, window in mkuf2.WINDOWS.items():
            with self.subTest(board=board):
                partitions = internal_partitions(board)
                self.assertEqual(partitions["softdevice_partition"][1], window.softdevice_end)
                self.assertEqual(partitions["boot_partition"][0], window.softdevice_end)
                self.assertEqual(partitions["slot0_partition"][1], window.user_flash_end)
                self.assertEqual(partitions["uf2_boot_partition"][0], window.bootloader_start)

    def test_only_mcuboot_and_slot0_lie_inside_a_window(self):
        for board, window in mkuf2.WINDOWS.items():
            for label, (start, end) in internal_partitions(board).items():
                if label in ("boot_partition", "slot0_partition"):
                    continue
                with self.subTest(board=board, partition=label):
                    self.assertTrue(end <= window.softdevice_end or start >= window.user_flash_end)

    def test_the_go_identity_page_is_refused(self):
        start, end = internal_partitions("t_echo_plus")["identity_partition"]
        problem = mkuf2.check_window([(GO.softdevice_end, end)], GO)
        self.assertIn("USER_FLASH_END", problem)
        self.assertEqual(start, GO.user_flash_end)


class RangeGuards(unittest.TestCase):
    def test_a_block_below_the_softdevice_end_is_refused(self):
        problem = mkuf2.check_window([(0x00025000, 0x00026000)], GO)
        self.assertIsNotNone(problem)
        self.assertIn("MBR/SoftDevice", problem)

    def test_a_block_one_byte_below_the_softdevice_end_is_refused(self):
        problem = mkuf2.check_window([(0x00025FFF, 0x00030000)], GO)
        self.assertIsNotNone(problem)
        self.assertIn("MBR/SoftDevice", problem)

    def test_a_go_image_is_refused_on_the_x1_softdevice(self):
        # The Go's MCUboot starts at 0x26000, the last page of S140 7.3.0 on an X1.
        problem = mkuf2.check_window([(0x00026000, 0x00034000), (0x00034000, 0x000EA000)], BRAVO)
        self.assertIn("MBR/SoftDevice (S140 7.3.0)", problem)

    def test_a_block_above_user_flash_end_is_refused(self):
        for window in (GO, BRAVO):
            problem = mkuf2.check_window([(window.softdevice_end, 0x000EA001)], window)
            self.assertIn("USER_FLASH_END", problem)

    def test_a_block_in_the_storage_area_is_refused_for_the_write_ceiling(self):
        for window in (GO, BRAVO):
            problem = mkuf2.check_window([(window.softdevice_end, 0x000F4000)], window)
            self.assertIn("USER_FLASH_END", problem)

    def test_a_block_reaching_the_factory_bootloader_names_it(self):
        for window in (GO, BRAVO):
            problem = mkuf2.check_window([(window.softdevice_end, 0x000F4001)], window)
            self.assertIn("0x0F4000", problem)
            self.assertIn("bootloader", problem)

    def test_an_image_ending_exactly_at_user_flash_end_is_accepted(self):
        # The real case: imgtool pads the confirmed image to slot1's length, so
        # the trailer lands on the last byte before the ceiling.
        for window in (GO, BRAVO):
            self.assertIsNone(mkuf2.check_window([(window.softdevice_end, 0x000EA000)], window))

    def test_the_two_sysbuild_images_together_are_accepted(self):
        self.assertIsNone(mkuf2.check_window([
            (0x00026000, 0x00034000),
            (0x00034000, 0x000EA000),
        ], GO))
        self.assertIsNone(mkuf2.check_window([
            (0x00027000, 0x00035000),
            (0x00035000, 0x000EA000),
        ], BRAVO))

    def test_a_board_without_a_window_is_refused_before_any_input_is_read(self):
        done = subprocess.run([sys.executable, mkuf2.__file__, "nrf52840dk", "out.uf2", "missing.hex"],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 1)
        self.assertIn("no write window for board 'nrf52840dk'", done.stdout)


class Merging(unittest.TestCase):
    def test_two_images_merge_into_one_byte_map(self):
        with tempfile.TemporaryDirectory() as d:
            a = hex_file(span(0x00026000, 32, 0x11), d, "a.hex")
            b = hex_file(span(0x00034000, 32, 0x22), d, "b.hex")
            merged = {}
            mkuf2.read_hex(a, merged)
            mkuf2.read_hex(b, merged)
            self.assertEqual(len(merged), 64)
            self.assertEqual(merged[0x00026000], 0x11)
            self.assertEqual(merged[0x00034010], 0x22)

    def test_conflicting_bytes_at_the_same_address_fail(self):
        with tempfile.TemporaryDirectory() as d:
            a = hex_file(span(0x00026000, 16, 0x11), d, "a.hex")
            b = hex_file(span(0x00026000, 16, 0x22), d, "b.hex")
            merged = {}
            mkuf2.read_hex(a, merged)
            with self.assertRaises(SystemExit):
                mkuf2.read_hex(b, merged)

    def test_identical_bytes_at_the_same_address_are_not_a_conflict(self):
        with tempfile.TemporaryDirectory() as d:
            a = hex_file(span(0x00026000, 16, 0x11), d, "a.hex")
            merged = {}
            mkuf2.read_hex(a, merged)
            mkuf2.read_hex(a, merged)
            self.assertEqual(len(merged), 16)

    def test_ranges_of_splits_on_a_gap_and_keeps_a_run_whole(self):
        byte_map = span(0x1000, 16)
        byte_map.update(span(0x2000, 16))
        self.assertEqual(mkuf2.ranges_of(byte_map),
                         [(0x1000, 0x1010), (0x2000, 0x2010)])
        self.assertEqual(mkuf2.ranges_of({}), [])


class IntelHex(unittest.TestCase):
    def test_a_map_round_trips_through_write_hex_and_read_hex(self):
        original = span(0x00026000, 300, 0x5A)
        original.update(span(0x000E9F00, 256, 0xC3))
        with tempfile.TemporaryDirectory() as d:
            back = {}
            mkuf2.read_hex(hex_file(original, d), back)
            self.assertEqual(back, original)

    def test_an_address_above_64k_gets_an_extended_linear_record(self):
        original = span(0x0003FFF0, 0x20, 0x7E)
        with tempfile.TemporaryDirectory() as d:
            path = hex_file(original, d)
            text = path.read_text()
            back = {}
            mkuf2.read_hex(path, back)
        self.assertEqual(back, original)
        # Type 04 carries the upper 16 bits; the run crosses 0x40000, so the
        # writer has to emit a second one mid-run or everything after the
        # boundary reads back 64 KB low.
        records = [line for line in text.splitlines() if line[7:9] == "04"]
        self.assertEqual([line[9:13] for line in records], ["0003", "0004"])

    def test_the_last_line_is_the_end_of_file_record(self):
        with tempfile.TemporaryDirectory() as d:
            path = hex_file(span(0x00026000, 16), d)
            self.assertEqual(path.read_text().splitlines()[-1], ":00000001FF")

    def test_every_record_carries_a_valid_checksum(self):
        with tempfile.TemporaryDirectory() as d:
            path = hex_file(span(0x00026000, 300), d)
            for line in path.read_text().splitlines():
                raw = bytes.fromhex(line[1:])
                self.assertEqual(sum(raw) & 0xFF, 0, line)

    def test_an_extended_segment_record_offsets_by_sixteen(self):
        # Type 02 is not what Zephyr emits, and the parser accepts it anyway;
        # a wrong shift here would place a whole image 15/16ths too low.
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "seg.hex"
            path.write_text(":020000021000EC\n:0100000041BE\n:00000001FF\n")
            back = {}
            mkuf2.read_hex(path, back)
            self.assertEqual(back, {0x10000: 0x41})


def tree_with(directory, relative, text):
    path = pathlib.Path(directory) / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return directory


class NoNordicDfuPackage(unittest.TestCase):
    def test_the_tree_as_committed_ships_no_dfu_package(self):
        self.assertEqual(check_no_dfu_package.findings(), [])

    def test_a_workflow_staging_a_zip_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            tree_with(d, ".github/workflows/release.yml", "  path: build/out/skyblip-go.zip\n")
            found = check_no_dfu_package.findings(d)
        self.assertEqual(found, [(".github/workflows/release.yml", 1,
                                  "stages a .zip firmware package")])

    def test_a_script_calling_adafruit_nrfutil_genpkg_is_refused_on_both_counts(self):
        with tempfile.TemporaryDirectory() as d:
            tree_with(d, "scripts/package.sh", "echo ok\nadafruit-nrfutil dfu genpkg --dev-type 0x0052\n")
            found = check_no_dfu_package.findings(d)
        self.assertEqual([(path, line) for path, line, _ in found],
                         [("scripts/package.sh", 2), ("scripts/package.sh", 2)])

    def test_the_uf2_install_path_is_not_refused(self):
        with tempfile.TemporaryDirectory() as d:
            tree_with(d, ".github/workflows/release.yml",
                      "  run: python3 ../scripts/mkuf2.py build/skyblip-go.uf2 a.hex b.hex\n")
            self.assertEqual(check_no_dfu_package.findings(d), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
