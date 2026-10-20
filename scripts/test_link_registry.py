#!/usr/bin/env python3
"""Self-check for link_registry.py: the check that holds the generated files to the registry.

    python3 scripts/test_link_registry.py

Stdlib and clang-format (CLANG_FORMAT names it), as check_link_registry.py needs.
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import link_registry  # noqa: E402

EXPECTED = link_registry.written(link_registry.load(), link_registry.formatter())


def committed_with(relative, before, after):
    text = link_registry.committed(relative)
    assert before in text, "%s no longer holds %r" % (relative, before)
    mutated = {relative: text.replace(before, after, 1)}
    return lambda name: mutated.get(name, link_registry.committed(name))


class Check(unittest.TestCase):
    def test_the_committed_files_are_what_the_registry_writes(self):
        self.assertEqual(link_registry.stale(EXPECTED, link_registry.committed), [])

    # A whitespace-blind comparison passed an enumeration named "unknown_ command".
    def test_a_space_inside_a_javascript_string_is_a_stale_module(self):
        read = committed_with(link_registry.JAVASCRIPT_MODULE, '"unknown_command"',
                              '"unknown_ command"')
        self.assertEqual(link_registry.stale(EXPECTED, read), [link_registry.JAVASCRIPT_MODULE])

    def test_a_space_inside_a_cpp_string_is_a_stale_header(self):
        read = committed_with(link_registry.NAMES_HEADER, '"unknown_command"', '"unknown_ command"')
        self.assertEqual(link_registry.stale(EXPECTED, read), [link_registry.NAMES_HEADER])

    def test_a_cpp_header_laid_out_other_than_clang_format_lays_it_is_stale(self):
        read = committed_with(link_registry.FIRMWARE_HEADER, "enum class Command : uint8_t {\n",
                              "enum class Command : uint8_t\n{\n")
        self.assertEqual(link_registry.stale(EXPECTED, read), [link_registry.FIRMWARE_HEADER])


if __name__ == "__main__":
    unittest.main(verbosity=2)
