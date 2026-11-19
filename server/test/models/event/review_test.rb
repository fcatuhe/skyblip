require "test_helper"

class Event::ReviewTest < ActiveSupport::TestCase
  # A pilot whose last step was a failure gave up there, and the admin can reach them before they write in.
  test "a browser whose last step was a failure is stranded, and one that recovered is not" do
    journey "STUCK000", "device.connected", 3.minutes.ago
    journey "STUCK000", "device.noticed", 2.minutes.ago, key: "link_lost"
    journey "FINE0000", "device.noticed", 3.minutes.ago, key: "link_lost"
    journey "FINE0000", "device.connected", 2.minutes.ago
    journey "SAVED000", "device.noticed", 2.minutes.ago, key: "saved"
    journey "CHOOSER0", "bluetooth.chooser_closed", 1.minute.ago, filter: "new"

    stranded = Event::Review.new.stranded

    assert_equal 2, stranded[:count]
    assert_equal({ "bluetooth.chooser_closed new" => 1, "device.noticed link_lost" => 1 }, stranded[:by_reason])
    assert_equal %w[CHOOSER0 STUCK000], stranded[:latest].pluck(:browser)
  end

  test "installs count those started, those that came back on the new version, and the causes of those that did not" do
    journey "BROWSER0", "device.task_started", 30.minutes.ago, task: "install"
    journey "BROWSER0", "device.task_started", 20.minutes.ago, task: "install"
    journey "BROWSER0", "device.installed", 25.minutes.ago, lasted_ms: 240_000
    journey "BROWSER0", "device.install_reported", 10.minutes.ago, cause: "probation_failed"

    installs = Event::Review.new.installs

    assert_equal [ 2, 1 ], installs.values_at(:started, :installed)
    assert_equal 4.0, installs[:minutes][:p50]
    assert_equal({ "probation_failed" => 1 }, installs[:failed_by_cause])
  end

  # Links dropped while ready were left out, and they were most of the drops pilots saw.
  test "links that dropped unasked are counted by phase, beside the reasons the units gave" do
    journey "BROWSER0", "device.disconnected", 30.minutes.ago, phase: "ready", expected: false
    journey "BROWSER0", "device.disconnected", 20.minutes.ago, phase: "uploading", expected: false
    journey "BROWSER0", "device.disconnected", 15.minutes.ago, phase: "ready", expected: true
    journey "BROWSER0", "device.drop_reported", 10.minutes.ago, reason: "timeout", code: 8

    devices = Event::Review.new.devices

    assert_equal({ "ready" => 1, "uploading" => 1 }, devices[:dropped_by_phase])
    assert_equal({ "uploading" => 1 }, devices[:dropped_while_busy])
    assert_equal({ "timeout" => 1 }, devices[:drop_reasons])
  end

  # A unit repeats its report on every connection until the next install, which counted one failure three times.
  test "a failed install is counted once however often its unit reports it, and a second try of the same image counts again" do
    report = { cause: "slot_damaged", from: "0.1.0+1677", to: "0.1.0+1693" }
    journey "BROWSER0", "device.install_reported", 40.minutes.ago, **report, tried: "0.1.0+1693"
    journey "BROWSER0", "device.install_reported", 38.minutes.ago, **report
    journey "BROWSER0", "device.install_reported", 20.minutes.ago, **report
    journey "BROWSER0", "device.install_reported", 10.minutes.ago, **report, tried: "0.1.0+1693"
    journey "BROWSER0", "device.install_reported", 5.minutes.ago, cause: "bootloader_refused", from: "0.1.0+1677", to: "0.1.0+1701"

    assert_equal({ "slot_damaged" => 2, "bootloader_refused" => 1 }, Event::Review.new.installs[:failed_by_cause])
  end

  test "browsers are counted once, and those without Bluetooth by browser and system" do
    journey "IPHONE00", "browser.opened", 2.days.ago, bluetooth: false, browser_name: "Safari", os_name: "iOS"
    journey "IPHONE00", "browser.opened", 1.day.ago, bluetooth: false, browser_name: "Safari", os_name: "iOS"
    journey "ANDROID0", "browser.opened", 1.day.ago, bluetooth: true, standalone: true, browser_name: "Chrome", os_name: "Android"

    browsers = Event::Review.new.browsers

    assert_equal [ 2, 1, 1 ], browsers.values_at(:total, :installed_app, :without_bluetooth)
    assert_equal({ "Safari on iOS" => 1 }, browsers[:without_bluetooth_on])
  end

  test "the daily cap is compared with the busiest day, and a cap under twice it is advised up to three times it" do
    freeze_time
    3.times { Event.create!(name: "unit.enrolled", occurred_at: 2.days.ago) }
    Event.create!(name: "unit.enrolled", occurred_at: 1.day.ago)

    assert_equal({ cap: 50, busiest_day: 3, covers: 16.7 }, Event::Review.new.daily_cap)

    Activation::Cap.current.update!(new_units_per_day: 5)
    assert_equal "raise it to 9: it covers less than 2 times the busiest day", Event::Review.new.daily_cap[:advice]
  end

  test "a period with no new unit gives no advice on the daily cap" do
    assert_equal({ cap: 50, busiest_day: 0 }, Event::Review.new.daily_cap)
  end

  private
    def journey(browser, name, at, **payload)
      Event.create! name:, payload:, context: { browser:, device: "go-00012a" }, occurred_at: at
    end
end
