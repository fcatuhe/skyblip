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
