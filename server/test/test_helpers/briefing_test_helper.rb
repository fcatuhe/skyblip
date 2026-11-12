module BriefingTestHelper
  SCREENS = { phone: [ 412, 915 ], laptop: [ 1280, 820 ] }.freeze
  NEW_SETTINGS = { aircraft_type: 1, alarm_enabled: true, alarm_volume: "loud", units: "nautical", callsign: "", own_icao: 0 }.freeze

  private
    def in_range(*units)
      script = Rails.root.join("test/system/fake_fleet.js").read
      units.each { |unit| script << "\nfakeFleet.add(#{unit.to_json});" }
      page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: script)
    end

    def a_new_go(**options) = { name: "activate 3F2A skyBlip", address: nil, vectors:, stored: NEW_SETTINGS, **options }
    def a_go(**options) = { name: "skyBlip Go 00012A", **options }
    def a_bravo(**options) = { name: "skyBlip Bravo 3F2A", product: "skyblip_bravo", address: nil, running: "0.1.0+40", key: "BBBBBBBB", imu: nil, **options }

    # INFO: fc 12nov26 a shelf frame still loading when the cassettes close would reach GitHub, so the block waits for it
    def on_the_shelf
      VCR.use_cassette("github/releases", allow_playback_repeats: true) do
        VCR.use_cassette("github/firmware", allow_playback_repeats: true) do
          yield
          assert_no_selector "turbo-frame#firmware_shelf:not([complete])", visible: :all
        end
      end
    end

    def unit(name) = "fakeFleet.unit(#{name.to_json})"

    # INFO: fc 12nov26 the page moves once it has read the newest image, so a test waits for that before it clicks anything
    def connect_go
      visit "/briefing"
      click_button "Connect a device"
      assert_selector ".unit-row", text: /is out|up to date/, wait: 15
    end

    # INFO: fc 11nov26 BRIEFING_SCREENS=dir saves every screen a test walks through, at phone and laptop sizes, for the pull request
    def screen(name)
      return unless (directory = ENV["BRIEFING_SCREENS"])

      SCREENS.each do |size, (width, height)|
        page.current_window.resize_to(width, height)
        page.save_screenshot(File.join(directory, "#{name}-#{size}.png"), full: size == :laptop)
      end
      page.current_window.resize_to(*ApplicationSystemTestCase::SCREEN_SIZE)
    end
end
