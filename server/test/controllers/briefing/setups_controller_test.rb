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
end
