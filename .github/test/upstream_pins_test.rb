require "minitest/autorun"
require "fileutils"
require "tmpdir"
require_relative "../../lib/upstream_pins"

class UpstreamPinsTest < Minitest::Test
  def test_the_workflows_read_emsdk_from_the_pins
    in_tree { |pins| assert_equal "6.0.9", pins.version("emscripten-core/emsdk") }
  end

  def test_pins_that_match_the_copies_in_use_report_nothing
    in_tree { |pins| assert_empty pins.mismatches }
  end

  def test_a_doctest_bump_fails_until_the_header_follows
    in_tree(doctest: "v2.5.4") do |pins|
      assert_equal 1, pins.mismatches.size
      assert_includes pins.mismatches.first, "doctest is v2.5.4 in .github/pins/action.yml and v2.5.3 in firmware/vendor/doctest/doctest.h"
      assert_includes pins.mismatches.first, "LOCAL-CHANGES.md"
    end
  end

  def test_a_zephyr_patch_bump_asks_for_a_check_and_a_minor_bump_for_a_migration
    in_tree(zephyr: "v4.4.3") { |pins| assert_includes pins.mismatches.first, "A patch bump" }
    in_tree(zephyr: "v4.5.0") do |pins|
      assert_includes pins.mismatches.first, "Zephyr is v4.5.0 in .github/pins/action.yml and v4.4.2 in firmware/west.yml"
      assert_includes pins.mismatches.first, "A minor bump"
    end
  end

  def test_a_name_the_pins_lack_fails_loudly
    in_tree { |pins| assert_raises(KeyError) { pins.version("emscripten-core/emscripten") } }
  end

  private
    def in_tree(doctest: "v2.5.3", zephyr: "v4.4.2")
      Dir.mktmpdir do |root|
        write(root, ".github/pins/action.yml", <<~YAML)
          runs:
            using: composite
            steps:
              - uses: emscripten-core/emsdk@6.0.9
              - uses: doctest/doctest@#{doctest}
              - uses: zephyrproject-rtos/zephyr@#{zephyr}
        YAML
        write(root, "firmware/vendor/doctest/doctest.h", <<~HEADER)
          #define DOCTEST_VERSION_MAJOR 2
          #define DOCTEST_VERSION_MINOR 5
          #define DOCTEST_VERSION_PATCH 3
          #define DOCTEST_VERSION_STR DOCTEST_TOSTR(DOCTEST_VERSION_MAJOR)
        HEADER
        write(root, "firmware/west.yml", <<~YAML)
          manifest:
            projects:
              - name: zephyr
                revision: v4.4.2
        YAML
        yield UpstreamPins.new(root)
      end
    end

    def write(root, path, content)
      FileUtils.mkdir_p(File.dirname(File.join(root, path)))
      File.write(File.join(root, path), content)
    end
end
