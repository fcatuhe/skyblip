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

  # A task ends on its notice, so a save, a capture or a pairing that worked was counted as refused and its pilot as stranded.
  test "a task that ended on its result is granted, and its pilot is not stranded" do
    journey "SAVED000", "device.task_started", 4.minutes.ago, task: "set"
    journey "SAVED000", "device.task_ended", 3.minutes.ago, task: "set", outcome: "saved"
    journey "PAIRED00", "device.task_started", 4.minutes.ago, task: "pair"
    journey "PAIRED00", "device.task_ended", 3.minutes.ago, task: "pair", outcome: "paired"
    journey "PAIRED00", "device.noticed", 2.minutes.ago, key: "paired"
    journey "REFUSED0", "device.task_started", 4.minutes.ago, task: "set"
    journey "REFUSED0", "device.task_ended", 3.minutes.ago, task: "set", outcome: "refused_on_device"

    review = Event::Review.new

    assert_equal({ started: 2, granted: 1, refused: { "refused_on_device" => 1 } }, review.tasks["set"])
    assert_equal({ started: 1, granted: 1, refused: {} }, review.tasks["pair"])
    assert_equal %w[REFUSED0], review.stranded[:latest].pluck(:browser)
  end

  # Insights left out the setup over USB, where 17 of 21 browsers that opened the port chooser in a week never picked a port.
  test "setups over USB count the browsers that reached each step, the notices they read and why they moved to a phone" do
    journey "CLOSED00", "installer.stepped", 9.minutes.ago, phase: "choosing_port"
    journey "CLOSED00", "installer.noticed", 8.minutes.ago, key: "port_closed"
    journey "CLOSED00", "installer.stepped", 7.minutes.ago, phase: "choosing_port"
    journey "CLOSED00", "installer.noticed", 6.minutes.ago, key: "port_closed"
    journey "PHONE000", "installer.stepped", 9.minutes.ago, phase: "choosing_port"
    journey "PHONE000", "installer.port_chosen", 8.minutes.ago, firmware: "meshtastic"
    journey "PHONE000", "installer.drive_chosen", 7.minutes.ago, product: "go"
    journey "PHONE000", "installer.wrote", 6.minutes.ago, product: "go"
    journey "PHONE000", "installer.handed_off", 5.minutes.ago, cause: "adapter"
    journey "DONE0000", "installer.stepped", 9.minutes.ago, phase: "choosing_port"
    journey "DONE0000", "installer.port_chosen", 8.minutes.ago, firmware: "bootloader"
    journey "DONE0000", "installer.drive_chosen", 7.minutes.ago, product: "go"
    journey "DONE0000", "installer.wrote", 6.minutes.ago, product: "go"
    journey "DONE0000", "installer.finished", 5.minutes.ago, kind: "go"

    review = Event::Review.new

    assert_equal({ started: 3, port_chosen: 2, drive_chosen: 2, wrote: 2, finished: 1 }, review.setups[:browsers])
    assert_equal({ "port_closed" => 2 }, review.setups[:noticed])
    assert_equal({ "adapter" => 1 }, review.setups[:handed_off])
    assert_equal [ 3, 1 ], review.headline.values_at(:setups_started, :setups_finished)
  end

  test "a setup that stopped on a notice leaves its browser stranded, and one that went on after it does not" do
    journey "CLOSED00", "installer.noticed", 3.minutes.ago, key: "port_closed"
    journey "RETRIED0", "installer.noticed", 3.minutes.ago, key: "port_closed"
    journey "RETRIED0", "installer.port_chosen", 2.minutes.ago, firmware: "bootloader"

    stranded = Event::Review.new.stranded

    assert_equal({ "installer.noticed port_closed" => 1 }, stranded[:by_reason])
    assert_equal %w[CLOSED00], stranded[:latest].pluck(:browser)
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

  test "agents' calls are counted by tool, and the sessions they open apart from them" do
    2.times { answered "initialize" }
    answered "notifications/initialized"
    answered "tools/list"
    2.times { answered "tools/call", tool: "list_errors" }
    answered "tools/call", tool: "get_insights"

    assert_equal({ sessions: 2, calls: { "list_errors" => 2, "get_insights" => 1 } }, Event::Review.new.agents)
  end

  test "the public MCP server's calls are counted apart from the admins' agents" do
    answered "initialize", path: "/mcp"
    2.times { answered "tools/call", tool: "search_pages", path: "/mcp" }
    answered "tools/call", tool: "list_errors"

    assert_equal({ sessions: 1, calls: { "search_pages" => 2 } }, Event::Review.new.connectors)
    assert_equal({ sessions: 0, calls: { "list_errors" => 1 } }, Event::Review.new.agents)
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
    def answered(method, tool: nil, path: "/admin/mcp")
      Event.create! name: "mcp.answered", payload: { method:, tool: }.compact, context: { path: }, occurred_at: 1.minute.ago
    end

    def journey(browser, name, at, **payload)
      Event.create! name:, payload:, context: { browser:, device: "go-00012a" }, occurred_at: at
    end
end
