require "test_helper"

class Briefing::InstallersControllerTest < ActionDispatch::IntegrationTest
  test "the installers name the newest .uf2 of each product, through our origin" do
    VCR.use_cassette("github/releases") do
      get "/briefing/installer"
    end

    assert_response :success
    assert_select "[data-installer=go] option", count: 1
    assert_select "[data-installer=go] option[data-version='0.1.0+1765'][value='/releases/dev-1765/skyblip-go-dev-1765.uf2']"
    assert_select "[data-installer=bravo] option[value='/releases/dev-1765/skyblip-bravo-dev-1765.uf2']"
  end

  test "a pilot who asked for development builds gets every one, the usual choice first" do
    cookies[:development_builds] = "true"
    VCR.use_cassette("github/releases") do
      get "/briefing/installer"
    end

    assert_select "[data-installer=go] option" do |options|
      assert_equal %w[0.1.0+1765 0.1.0+1758 0.1.0+1748 0.1.0+1673], options.map { it["data-version"] }
      assert_equal "skyBlip dev 0.1.0+1673 (feat-bravo-adsb-trial)", options.last.text
    end
  end

  # The cassette records GitHub rejecting an invalid token with HTTP 401.
  test "a GitHub failure says the images did not load" do
    VCR.use_cassette("github/unavailable") do
      get "/briefing/installer"
    end

    assert_select "turbo-frame#installers [data-installers-unavailable]"
    assert_select "[data-installer]", count: 0
  end
end
