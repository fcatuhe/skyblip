require "yaml"

class UpstreamPins
  PINS = ".github/pins/action.yml".freeze
  DOCTEST = "doctest/doctest".freeze
  ZEPHYR = "zephyrproject-rtos/zephyr".freeze
  DOCTEST_HEADER = "firmware/vendor/doctest/doctest.h".freeze
  WEST_MANIFEST = "firmware/west.yml".freeze

  def initialize(root)
    @root = root
  end

  def version(name)
    pinned.fetch(name) { raise KeyError, "#{PINS} pins no #{name}" }
  end

  def mismatches
    [ doctest_mismatch, zephyr_mismatch ].compact
  end

  private
    def pinned
      @pinned ||= YAML.load_file(File.join(@root, PINS)).dig("runs", "steps").to_h { |step| step.fetch("uses").split("@", 2) }
    end

    def doctest_mismatch
      wanted = version(DOCTEST)
      if doctest_in_use != wanted
        "doctest is #{wanted} in #{PINS} and #{doctest_in_use} in #{DOCTEST_HEADER}. Vendor the new header " \
          "and re-apply the patches firmware/vendor/doctest/LOCAL-CHANGES.md records."
      end
    end

    def zephyr_mismatch
      wanted = version(ZEPHYR)
      if zephyr_in_use != wanted
        "Zephyr is #{wanted} in #{PINS} and #{zephyr_in_use} in #{WEST_MANIFEST}. #{zephyr_advice(zephyr_in_use, wanted)}"
      end
    end

    def zephyr_advice(used, wanted)
      if minor(used) == minor(wanted)
        "A patch bump is bugfixes on the same minor and usually leaves the modules where they were: diff Zephyr's " \
          "west.yml for the modules we allow, read the release notes for BLE, MCUmgr, MCUboot, NVS, the task " \
          "watchdog and the nRF52 drivers, build, and check the flash budget."
      else
        "A minor bump moves every module with it (hal_nordic, mbedtls, MCUboot), so treat it as a migration: " \
          "build, check the flash budget, and exercise the device before merging."
      end
    end

    def minor(version) = version.split(".").first(2)

    def doctest_in_use
      header = File.read(File.join(@root, DOCTEST_HEADER))
      "v" + %w[MAJOR MINOR PATCH].map { |part| header[/^#define DOCTEST_VERSION_#{part} (\d+)$/, 1] }.join(".")
    end

    def zephyr_in_use
      manifest = YAML.load_file(File.join(@root, WEST_MANIFEST))
      manifest.dig("manifest", "projects").find { |project| project["name"] == "zephyr" }.fetch("revision")
    end
end
