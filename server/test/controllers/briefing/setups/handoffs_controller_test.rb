require "test_helper"

class Briefing::Setups::HandoffsControllerTest < ActionDispatch::IntegrationTest
  test "the code a computer shows opens the setup on a phone, on the unit it installed" do
    get "/briefing/setup/handoff", params: { installed: "go", tag: "3F2A" }

    assert_response :success
    assert_equal "image/svg+xml", response.media_type
    assert_equal Briefing::Handoff.new(product: "go").code("http://www.example.com/briefing/setup?installed=go&tag=3f2a"), response.body
  end

  test "a code is drawn only for a product" do
    get "/briefing/setup/handoff", params: { installed: "https://evil.example" }

    assert_response :not_found
  end
end
