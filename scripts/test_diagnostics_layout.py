#!/usr/bin/env python3
"""Self-check for diagnostics_layout.py and check_diagnostics_layout.py.

    python3 scripts/test_diagnostics_layout.py

Stdlib, and a C++ compiler (CXX names it) for the mirrors.
"""
import copy
import json
import os
import pathlib
import subprocess
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import check_diagnostics_layout  # noqa: E402
import diagnostics_layout  # noqa: E402

LAYOUT = diagnostics_layout.load()
EXPECTED = diagnostics_layout.generated(LAYOUT)
SCHEMA = json.loads(check_diagnostics_layout.SCHEMA.read_text(encoding="utf-8"))


def committed_with(relative, before, after):
    text = diagnostics_layout.committed(relative)
    assert before in text, "%s no longer holds %r" % (relative, before)
    mutated = {relative: text.replace(before, after, 1)}
    return lambda name: mutated.get(name, diagnostics_layout.committed(name))


def field(layout, type_name, field_name):
    kind = next(kind for kind in layout["types"] if kind["name"] == type_name)
    return next(field for field in kind["fields"] if field["name"] == field_name)


class Check(unittest.TestCase):
    def test_the_committed_files_are_what_the_layout_writes(self):
        self.assertEqual(diagnostics_layout.stale(EXPECTED, diagnostics_layout.committed), [])

    def test_a_space_inside_a_javascript_string_is_a_stale_module(self):
        read = committed_with(diagnostics_layout.JAVASCRIPT_MODULE, '"bad_crc"', '"bad_ crc"')
        self.assertEqual(diagnostics_layout.stale(EXPECTED, read),
                         [diagnostics_layout.JAVASCRIPT_MODULE])

    def test_an_offset_edited_in_the_python_tables_is_a_stale_module(self):
        read = committed_with(diagnostics_layout.PYTHON_MODULE, '"kind": "u32", "at": 0}',
                              '"kind": "u32", "at": 1}')
        self.assertEqual(diagnostics_layout.stale(EXPECTED, read),
                         [diagnostics_layout.PYTHON_MODULE])

    def test_a_cpp_header_laid_out_other_than_the_generator_lays_it_is_stale(self):
        read = committed_with(diagnostics_layout.FIRMWARE_HEADER, "enum class Type : uint8_t {\n",
                              "enum class Type : uint8_t\n{\n")
        self.assertEqual(diagnostics_layout.stale(EXPECTED, read),
                         [diagnostics_layout.FIRMWARE_HEADER])


class Layout(unittest.TestCase):
    def refusal(self, mutate):
        layout = copy.deepcopy(LAYOUT)
        mutate(layout)
        with self.assertRaises(ValueError) as caught:
            diagnostics_layout.check(layout)
        return str(caught.exception)

    def test_two_fields_on_one_payload_byte_are_refused(self):
        def mutate(layout):
            field(layout, "gnss", "residual_m")["at"] = 1
        self.assertIn("overlaps gnss.solution_phase_ms", self.refusal(mutate))

    # A retired field's bytes are never reused, because older captures still hold its value there.
    def test_a_live_field_on_a_retired_one_is_refused(self):
        def mutate(layout):
            field(layout, "power", "sample_offset_mv")["at"] = 14
        self.assertIn("overlaps power.trim_offset_mv", self.refusal(mutate))

    def test_two_bit_fields_of_one_byte_may_share_it_when_their_bits_do_not(self):
        diagnostics_layout.check(LAYOUT)
        self.assertEqual(field(LAYOUT, "adsb", "adsb_version")["at"],
                         field(LAYOUT, "adsb", "gva")["at"])

    def test_a_flag_on_an_envelope_bit_is_refused(self):
        def mutate(layout):
            field(layout, "gnss", "fix_valid")["bit"] = 1
        self.assertIn("bits 2 to 7", self.refusal(mutate))

    def test_a_field_past_the_payload_is_refused(self):
        def mutate(layout):
            field(layout, "pps", "since_edge_ms")["at"] = 15
        self.assertIn("run past the 16-byte payload", self.refusal(mutate))

    # The decoders index a tuple by the code, so a member inserted mid-list would rename every one after it.
    def test_an_enum_whose_codes_are_not_its_positions_is_refused(self):
        def mutate(layout):
            layout["enums"]["verdict"]["values"].insert(
                5, {"name": "inserted", "code": 16, "mirrors": "Inserted"})
        self.assertIn("verdict: codes", self.refusal(mutate))

    def test_valid_when_naming_a_payload_field_is_refused(self):
        def mutate(layout):
            field(layout, "adsb", "gva")["valid_when"] = "pressure_altitude_ft"
        self.assertIn("which is no flag of the type", self.refusal(mutate))

    def test_a_vouching_bit_resolves_into_the_decoder_tables(self):
        _, types = diagnostics_layout.decoder_tables(LAYOUT)
        traffic = {field["name"]: field for code, name, fields in types if name == "traffic"
                   for field in fields}
        self.assertEqual((traffic["relative_altitude_m"]["valid_bit"],
                          traffic["relative_altitude_m"]["vouch_bit"]), (7, 6))
        self.assertEqual(traffic["relative_altitude_valid"]["valid_bit"], 6)
        self.assertNotIn("altitude_recorded", traffic)


class DecodedView(unittest.TestCase):
    def test_the_schema_as_it_stands_names_what_the_decoders_write(self):
        self.assertEqual(list(check_diagnostics_layout.schema_failures(LAYOUT, SCHEMA)), [])

    def test_a_schema_enum_out_of_code_order_is_named(self):
        schema = copy.deepcopy(SCHEMA)
        schema["properties"]["band"]["enum"] = ["O", "M"]
        failures = list(check_diagnostics_layout.schema_failures(LAYOUT, schema))
        self.assertEqual(len(failures), 2, failures)
        self.assertIn("band lists ['O', 'M']", failures[0])

    def test_a_decoded_field_the_schema_lacks_and_a_declared_one_nobody_decodes_are_named(self):
        schema = copy.deepcopy(SCHEMA)
        schema["properties"]["spare"] = schema["properties"].pop("worst_tick_ms")
        failures = list(check_diagnostics_layout.schema_failures(LAYOUT, schema))
        self.assertEqual(failures, ["loop.worst_tick_ms is decoded and not declared",
                                    "spare is declared and no type decodes it"])

    def test_a_code8_field_capped_below_its_enum_is_named(self):
        schema = copy.deepcopy(SCHEMA)
        schema["properties"]["alarm"]["maximum"] = 0
        failures = list(check_diagnostics_layout.schema_failures(LAYOUT, schema))
        self.assertEqual(len(failures), 2, failures)
        self.assertIn("alarm caps at 0", failures[0])


def compiled(source):
    firmware = diagnostics_layout.ROOT / "firmware"
    return subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-fsyntax-only",
                           "-I%s" % firmware, "-I%s" % (firmware / "vendor"), "-x", "c++", "-"],
                          input=source, capture_output=True, text=True)


class Mirrors(unittest.TestCase):
    def test_the_layout_as_it_stands_compiles_against_the_cpp_enums(self):
        result = compiled(diagnostics_layout.mirrors_test(LAYOUT))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_cpp_member_the_layout_lacks_fails_the_build(self):
        layout = copy.deepcopy(LAYOUT)
        layout["enums"]["verdict"]["values"].pop()
        result = compiled(diagnostics_layout.mirrors_test(layout))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Busy", result.stderr)

    def test_a_code_the_cpp_enum_does_not_hold_fails_the_build(self):
        layout = copy.deepcopy(LAYOUT)
        values = layout["enums"]["band"]["values"]
        values[0]["mirrors"], values[1]["mirrors"] = values[1]["mirrors"], values[0]["mirrors"]
        result = compiled(diagnostics_layout.mirrors_test(layout))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("band M", result.stderr)

    def test_the_firmware_header_compiles(self):
        result = compiled(EXPECTED[diagnostics_layout.FIRMWARE_HEADER])
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
