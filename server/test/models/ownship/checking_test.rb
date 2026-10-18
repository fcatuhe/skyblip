require "test_helper"

class Ownship::CheckingTest < ActiveSupport::TestCase
  FIRMWARE_HEADER = Rails.root.join("../firmware/ui/widgets/skyship.h")

  test "the envelope is the box the firmware lays its screens out around" do
    header = FIRMWARE_HEADER.read

    assert_equal header[/kSkyshipSpan = (\d+)/, 1].to_i, Ownship::SPAN
    assert_equal header[/kSkyshipRows = (\d+)/, 1].to_i, Ownship::ROWS
    assert_equal header[/kSkyshipRowsToNose = (\d+)/, 1].to_i, Ownship::ROWS_TO_NOSE
  end

  test "every drawn type fits the device" do
    Ownship::AircraftType.all.select(&:drawn?).each do |type|
      assert type.ownship.fits_device?, "#{type.key}: #{type.ownship.problems.map(&:key).join(", ")}"
    end
  end

  # The browser editor reads the same file, so the two checkers cannot drift apart.
  test "each shared case reads and checks as the fixture says" do
    JSON.parse(file_fixture("ownship_checks.json").read).each do |example|
      ownship = Ownship.parse(example["text"])
      found = ownship.problems.map { { "key" => it.key.to_s, "details" => it.details.stringify_keys } }

      assert_equal example["hot_spot"], ownship.hot_spot, example["why"]
      assert_equal example["problems"], found, example["why"]
    end
  end
end
