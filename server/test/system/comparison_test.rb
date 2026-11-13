require "application_system_test_case"

class ComparisonTest < ApplicationSystemTestCase
  OWNSHIP = "0A 00 AB CD EF 1F EF 00 A8 89 78 0F 09 A9 07 B0 01 20 01 20 20 20 20 20 20 20 20 00".delete(" ")
  N825V = "14 00 AB 45 49 1F EF 15 A8 89 78 0F 09 A9 07 B0 01 20 01 4E 38 32 35 56 20 20 20 00".delete(" ")

  test "an aircraft both receivers hear is one row, each lists every attribute it decoded, and picking it compares them" do
    page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: Rails.root.join("test/system/fake_receivers.js").read)
    visit "/briefing/comparison"

    find("button[data-comparison-source-param=retro]", text: "Connect").click
    assert_selector "[data-source=retro].state", text: "USB CH340 7523 at 38400 baud"
    find("button[data-comparison-source-param=bravo]", text: "Connect").click
    assert_selector "[data-source=bravo].state", text: "skyBlip Bravo 3F2A over Nordic UART"

    page.execute_script("fakeReceivers.retro.send(#{OWNSHIP.to_json}); fakeReceivers.retro.send(#{N825V.to_json})")
    page.execute_script("fakeReceivers.bravo.send('GPRMC,101530.00,A,4454.400,N,12259.693,W,95.2,271.5,201126,,,A')")
    page.execute_script("fakeReceivers.bravo.send('PFLAA,0,25,0,0,1,AB4549!N825V,45,,63,0.3,8,0,1,')")

    assert_selector "[data-comparison-target=aircraft] tr", text: /AB4549\s+N825V\s+retro \+ bravo/
    assert_selector "[data-comparison-target=attributes][data-source=retro] td", text: "true track"
    assert_selector "[data-comparison-target=attributes][data-source=bravo] td", text: "ADS-B"
    assert_selector "[data-comparison-target=tally]", text: "1 aircraft heard: 1 by both"

    click_button "AB4549"
    assert_selector "[data-comparison-target=focusTitle]", text: "N825V · AB4549 · light"
    assert_selector "[data-comparison-target=focusRows] tr", text: /Ground speed, kt\s+123\s+122\s+\+1/
    assert_selector "[data-comparison-target=focusRows] tr", text: /New positions\s+1, .*not told apart in NMEA/
  end
end
