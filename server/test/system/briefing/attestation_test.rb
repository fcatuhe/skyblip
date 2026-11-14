require "application_system_test_case"

class Briefing::AttestationTest < ApplicationSystemTestCase
  test "an activated Go signs the firmware it runs on connecting, and the server records it under the unit" do
    unit = Unit.create!(device_id: vectors["device_id"], serial: 0x12a)
    unit.keys.create!(public_key: vectors["unit_public_key"], status: :active)
    in_range a_go(vectors:)

    on_the_shelf do
      connect_go
      page.document.synchronize { unit.firmwares.exists? || raise(Capybara::ExpectationNotMet, "no firmware reached the server") }
    end

    assert_equal [ [ "0.1.0+1000", false ] ], unit.firmwares.pluck(:version, :release)
  end
end
