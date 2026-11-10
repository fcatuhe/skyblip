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


def configure(activation, imu_image):
    with tempfile.TemporaryDirectory() as build:
        subprocess.run(
            ["cmake", "-S", str(PROJECT), "-B", build,
             f"-DCONFIG_SKYBLIP_ACTIVATION={'ON' if activation else 'OFF'}",
             f"-DCONFIG_SKYBLIP_IMU_IMAGE={'ON' if imu_image else 'OFF'}",
             "-DCONFIG_SKYBLIP_IMU_IMAGE_LINKED=OFF"],
            check=True, capture_output=True, text=True)
        built = pathlib.Path(build)
        sources = [pathlib.Path(line).resolve() for line in (built / "app_sources.txt").read_text().split()]
        platform = {source.name for source in sources if source.parent == PLATFORM}
        generated = {path.name for path in (built / "zephyr" / "include" / "generated").glob("*")}
        return platform, generated


class SharedLayers(unittest.TestCase):
    def test_a_product_without_activation_or_a_hub_builds_neither(self):
        platform, generated = configure(activation=False, imu_image=False)
        self.assertEqual(platform, {"link.cpp", "mcumgr_hooks.cpp"})
        self.assertEqual(generated, set())

    def test_the_go_builds_its_unit_key_and_pins_its_hub_image(self):
        platform, generated = configure(activation=True, imu_image=True)
        self.assertEqual(platform, {"identity.cpp", "imu_firmware.cpp", "link.cpp", "mcumgr_hooks.cpp"})
        self.assertEqual(generated, {"skyblip_server_key.inc", "bhi260ap_fw_sha256.inc"})


if __name__ == "__main__":
    unittest.main()
