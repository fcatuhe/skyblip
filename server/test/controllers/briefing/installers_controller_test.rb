require "test_helper"

class Briefing::InstallersControllerTest < ActionDispatch::IntegrationTest
  test "the installers name the newest .uf2 of each product, through our origin" do
    VCR.use_cassette("github/releases") do
      get "/briefing/installer"
    end

    assert_response :success
    assert_select "a[data-installer=go][data-version='0.1.0+1765'][href='/releases/dev-1765/skyblip-go-dev-1765.uf2']"
    assert_select "a[data-installer=bravo][href='/releases/dev-1765/skyblip-bravo-dev-1765.uf2']"
  end

  # The cassette records GitHub rejecting an invalid token with HTTP 401.
  test "a GitHub failure says the images did not load" do
    VCR.use_cassette("github/unavailable") do
      get "/briefing/installer"
    end

    assert_select "turbo-frame#installers [data-installers-unavailable]"
    assert_select "a[data-installer]", count: 0
  end
end
