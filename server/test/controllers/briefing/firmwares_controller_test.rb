require "test_helper"

class Briefing::FirmwaresControllerTest < ActionDispatch::IntegrationTest
  test "the update page renders the file picker and a separate shelf request without GitHub" do
    get "/update"

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
    assert_select "option[data-product=skyblip_go][value*='/skyblip-go-']", minimum: 1
    assert_select "option:not([data-product=skyblip_go])[data-channel]", count: 0
    images = css_select("option[data-kind]")
    assert_equal %w[slim slim slim full full full], images.map { |option| option["data-kind"] }
    assert_equal "/releases/dev-1005/skyblip-go-dev-1005.signed.bin", images.first["value"]
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
