require "test_helper"

class Releases::AssetsControllerTest < ActionDispatch::IntegrationTest
  test "firmware is fetched through our origin and has immutable caching" do
    VCR.use_cassette("github/firmware") do
      get "/releases/dev-1005/skyblip-go-dev-1005.signed.bin"
    end

    assert_response :success
    assert_equal "application/octet-stream", response.media_type
    assert_includes response.headers["Cache-Control"], "immutable"
    assert response.body.bytesize > 1000
  end

  test "stable release tags, whose names hold dots, route to the proxy" do
    route = Rails.application.routes.recognize_path("/releases/v0.1.0/skyblip_simulator.wasm", method: :get)

    assert_equal "releases/assets", route.fetch(:controller)
    assert_equal "v0.1.0", route.fetch(:release_tag)
    assert_equal "skyblip_simulator.wasm", route.fetch(:file)
  end

  test "only release tags and approved file names can be fetched" do
    [ "/releases/main/config.yml", "/releases/dev-1005/console.html", "/releases/dev-1005/evil.js" ].each do |path|
      get path
      assert_response :not_found
    end
  end
end
