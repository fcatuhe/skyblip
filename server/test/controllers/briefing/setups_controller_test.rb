require "test_helper"

class Briefing::SetupsControllerTest < ActionDispatch::IntegrationTest
  test "the setup page asks for the installers on its own request, so it renders without GitHub" do
    get "/briefing/setup"

    assert_response :success
    assert_select "turbo-frame#installers[src='/briefing/installer']"
    assert_select "button", text: "Install"
  end

  test "a partner named for the pairing is kept only when it is a device slug" do
    get "/briefing/setup", params: { for: "go-000003" }
    assert_select "[data-setup-partner-value='go-000003']"

    get "/briefing/setup", params: { for: "../admin" }
    assert_select "[data-setup-partner-value]", count: 0
  end

  test "a unit installed on another device opens the setup on its Connect step" do
    get "/briefing/setup", params: { installed: "go", tag: "3F2A", build: "0.8.1+2708" }
    assert_select "[data-setup-installed-value='go'][data-setup-tag-value='3f2a'][data-setup-build-value='0.8.1+2708']"

    get "/briefing/setup", params: { installed: "router", tag: "3F2A" }
    assert_select "[data-setup-installed-value]", count: 0
  end

  test "skyblip.eu/setup, the address a pilot types on the other device, opens the setup" do
    get "/setup"

    assert_redirected_to "/briefing/setup"
  end
end
