require "application_system_test_case"

class UpdateTest < ApplicationSystemTestCase
  setup do
    script = Rails.root.join("test/system/fake_skyblip.js").read
    page.driver.browser.page.command("Page.addScriptToEvaluateOnNewDocument", source: script)
  end

  test "a Go is offered the Go images of its track, and a Bravo none of them" do
    on_the_shelf do
      visit "/update"
      connect_go
      assert_selector "[data-manage-target=track]", text: "development"
      assert_selector "option[data-product=skyblip_go]:not([hidden])", visible: :all, minimum: 1
    end

    VCR.use_cassette("github/releases") do
      visit "/update"
      page.execute_script("fakeSkyblip.product = 'skyblip_bravo'")
      click_button "Connect"
      assert_selector "[data-manage-target=track]", text: "development"
      assert_selector "option[data-product=skyblip_go]", visible: :all, minimum: 1
      assert_no_selector "option[data-product]:not([hidden])", visible: :all
    end
  end

  test "a Go without an address is told it is not activated" do
    on_the_shelf do
      visit "/update"
      page.execute_script("fakeSkyblip.address = null")
      connect_go
      assert_selector "[data-manage-target=notes] p", text: "This unit is not activated yet"
    end
  end

  test "the link opened here is still up on the settings page" do
    on_the_shelf do
      visit "/update"
      connect_go
      click_link "a page of their own"

      assert_current_path "/configure"
      assert_select "Alarm volume", selected: "Soft"
      assert_no_button "Connect"
      assert_selector ".site-session", text: /Connected to\s+skyBlip Go 00003/
    end
  end

  test "the header names the device connected, and its Disconnect closes the link on any briefing page" do
    on_the_shelf do
      visit "/update"
      connect_go
      click_link "Briefing"

      assert_current_path "/briefing"
      within(".site-session") { click_button "Disconnect" }
      assert_no_selector ".site-session"
      click_link "Change the settings of your device"
      assert_button "Connect"
    end
  end

  test "a phone that kept the smallest Bluetooth packet is told so, and what to do" do
    VCR.use_cassette("github/releases") do
      visit "/update"
      page.execute_script("fakeSkyblip.mtu = 23")
      click_button "Connect"
      assert_selector "[role=status]", text: "smallest packet size, 20 bytes. Disconnect and connect again"
    end
  end

  test "a Go starts on the newest slim image of its track" do
    on_the_shelf do
      visit "/update"
      connect_go
      assert_equal "/releases/dev-1005/skyblip-go-dev-1005.signed.bin", find("[data-manage-target=shelf]").value
    end
  end

  private
    def on_the_shelf(&)
      VCR.use_cassette("github/releases") { VCR.use_cassette("github/firmware", &) }
    end

    def connect_go
      click_button "Connect"
      assert_selector "[data-manage-target=chosen]", text: /\d/
    end
end
