require "test_helper"

class Briefing::DevicesControllerTest < ActionDispatch::IntegrationTest
  test "the app opens on the devices this browser knows, and links its manifest" do
    get briefing_root_path

    assert_response :success
    assert_select "nav.fleet--home[data-controller=fleet]"
    assert_select "button[aria-label=?]", "Connect a device"
    assert_select "link[rel=manifest][href='/briefing/manifest']"
    assert_select ".site-footer", count: 0
  end

  test "a device page names the device from its address, before any link" do
    get briefing_device_path(slug: "go-00012a")

    assert_response :success
    assert_select "h1", text: "skyBlip Go 00012A"
    assert_select "nav.fleet--rail[data-fleet-current-value=go-00012a]"
    assert_select "a[href='/briefing/devices/go-00012a/firmware']", minimum: 1
    assert_select "main script, main style, main [style], main [onclick]", count: 0
  end

  test "a Bravo page names the Bravo, in French too, at the same address" do
    cookies[:locale] = "fr"
    get briefing_device_path(slug: "bravo-3f2a")

    assert_response :success
    assert_select "html[lang=fr]"
    assert_select "h1", text: "skyBlip Bravo 3F2A"
    assert_select "link[rel=manifest][href='/briefing/manifest']"
  end

  test "an address that names no skyBlip is not a device page" do
    get "/briefing/devices/softrf-1a2b3c"

    assert_response :not_found
  end
end
