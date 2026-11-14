#!/usr/bin/env python3
"""Self-check for skyblip_shared_layers() in firmware/cmake/skyblip.cmake, configured with cmake alone.

    python3 scripts/test_shared_layers.py
"""
import pathlib
import subprocess
import tempfile
import unittest

FIRMWARE = pathlib.Path(__file__).resolve().parent.parent / "firmware"
PROJECT = FIRMWARE / "test" / "cmake" / "shared_layers"
PLATFORM = FIRMWARE / "hardware" / "platform" / "zephyr"
PROTOCOLS = FIRMWARE / "protocols"


def configure(activation, imu_image, central=False):
    with tempfile.TemporaryDirectory() as build:
        subprocess.run(
            ["cmake", "-S", str(PROJECT), "-B", build,
             f"-DCONFIG_SKYBLIP_ACTIVATION={'ON' if activation else 'OFF'}",
             f"-DCONFIG_SKYBLIP_IMU_IMAGE={'ON' if imu_image else 'OFF'}",
             f"-DCONFIG_BT_CENTRAL={'ON' if central else 'OFF'}",
             "-DCONFIG_SKYBLIP_IMU_IMAGE_LINKED=OFF"],
            check=True, capture_output=True, text=True)
        built = pathlib.Path(build)
        sources = [pathlib.Path(line).resolve() for line in (built / "app_sources.txt").read_text().split()]
        platform = {source.name for source in sources if source.parent == PLATFORM}
        generated = {path.name for path in (built / "zephyr" / "include" / "generated").glob("*")}
        protocols = {str(source.relative_to(PROTOCOLS)) for source in sources
                     if PROTOCOLS in source.parents}
        return platform, generated, protocols


class SharedLayers(unittest.TestCase):
    def test_a_product_without_activation_a_hub_or_a_central_builds_none(self):
        platform, generated, _ = configure(activation=False, imu_image=False)
        self.assertEqual(platform, {"link.cpp", "mcumgr_hooks.cpp"})
        self.assertEqual(generated, set())

    def test_the_go_builds_its_unit_key_its_central_and_pins_its_hub_image(self):
        platform, generated, _ = configure(activation=True, imu_image=True, central=True)
        self.assertEqual(platform, {"central.cpp", "identity.cpp", "imu_firmware.cpp", "link.cpp",
                                    "mcumgr_hooks.cpp"})
        self.assertEqual(generated, {"skyblip_server_key.inc", "bhi260ap_fw_sha256.inc"})

    def test_every_protocol_stack_goes_into_the_image_and_its_tests_do_not(self):
        _, _, protocols = configure(activation=False, imu_image=False)
        expected = {str(source.relative_to(PROTOCOLS)) for source in PROTOCOLS.glob("*/*.cpp")}
        self.assertTrue({"adsl/packet.cpp", "adsb/message.cpp", "alptas/frame.cpp",
                         "common/repair.cpp"} <= expected)
        self.assertEqual(protocols, expected)


if __name__ == "__main__":
    unittest.main()
