require "test_helper"

class Briefing::FirmwaresControllerTest < ActionDispatch::IntegrationTest
  test "the update page renders the file picker and a separate shelf request without GitHub" do
    get "/manage"

    assert_response :success
    assert_select "input[type=file]"
    assert_select "turbo-frame#firmware_shelf[src]"
  end

  test "the shelf is populated from published releases on its own request" do
    VCR.use_cassette("github/releases") do
      get "/briefing/firmwares"
    end

    assert_response :success
    assert_select "turbo-frame#firmware_shelf select[data-manage-target=shelf]"
    assert_select "option[data-channel=development]", minimum: 1
    assert_select "option[value^='/releases/dev-']", minimum: 1
  end

  # The cassette records GitHub rejecting an invalid token with HTTP 401.
  test "a GitHub failure leaves an explicit message instead of an empty shelf" do
    VCR.use_cassette("github/unavailable") do
      get "/briefing/firmwares"
    end

    assert_response :success
    assert_select "turbo-frame#firmware_shelf [role=status]", text: /unavailable/
    assert_select "select", count: 0
  end
end
