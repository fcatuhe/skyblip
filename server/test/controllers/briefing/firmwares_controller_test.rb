require "test_helper"

class Briefing::FirmwaresControllerTest < ActionDispatch::IntegrationTest
  test "the firmware page renders the file picker and a separate shelf request without GitHub" do
    get "/briefing/devices/go-00012a/firmware"

    assert_response :success
    assert_select "input[type=file]"
    assert_select "turbo-frame#firmware_shelf[src='/briefing/firmwares']"
  end

  test "the shelf is populated from published releases on its own request" do
    VCR.use_cassette("github/releases") do
      get "/briefing/firmwares"
    end

    assert_response :success
    assert_select "turbo-frame#firmware_shelf select[data-shelf]"
    assert_select "option[data-channel=development][data-key='212602F2']", minimum: 1
    assert_select "option[value*='/dev-1765/'][data-version='0.1.0+1765']", count: 3
    assert_select "option[value^='/releases/dev-']", minimum: 1
    assert_select "option[data-product=go][value*='/skyblip-go-']", minimum: 1
    assert_select "option[data-product=bravo][value*='/skyblip-bravo-']", minimum: 1
    images = css_select("option[data-kind]")
    assert_equal %w[base] * 8 + %w[full] * 4, images.map { |option| option["data-kind"] }
    assert_equal "/releases/dev-1765/skyblip-bravo-dev-1765.signed.bin", images.first["value"]
    assert_equal "/releases/dev-1765/skyblip-go-dev-1765.full.signed.bin", images[8]["value"]
    assert_equal [ "skyBlip dev 0.1.0+1765", "skyBlip dev 0.1.0+1765, with the motion sensor firmware" ], [ images.first.text, images[8].text ]
  end

  test "the shelf carries each main build's technical changes, newest first, for the page to pick from" do
    VCR.use_cassette("github/releases") do
      get "/briefing/firmwares"
    end

    sections = css_select("template[data-changes] section[data-channel=development]")
    assert_equal %w[0.1.0+1765 0.1.0+1758 0.1.0+1748], sections.map { |section| section["data-version"] }
    assert_select sections.first, "li a[href='https://github.com/fcatuhe/skyblip/pull/351']", text: "infra(deps): track emsdk, doctest and Zephyr with Dependabot"
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
