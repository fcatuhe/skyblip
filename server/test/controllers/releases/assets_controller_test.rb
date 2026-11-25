require "test_helper"

class Releases::AssetsControllerTest < ActionDispatch::IntegrationTest
  test "firmware is fetched through our origin and has immutable caching" do
    VCR.use_cassette("github/releases") do
      VCR.use_cassette("github/firmware") { get "/releases/dev-1765/skyblip-go-dev-1765.signed.bin" }
    end

    assert_response :success
    assert_equal "application/octet-stream", response.media_type
    assert_includes response.headers["Cache-Control"], "immutable"
    assert response.body.bytesize > 1000
  end

  test "a release's .uf2 is fetched through our origin, for a USB install to write" do
    VCR.use_cassette("github/installers") do
      get "/releases/dev-1860/skyblip-bravo-dev-1860.uf2"
    end

    assert_response :success
    assert_equal "application/octet-stream", response.media_type
    assert_equal 0, response.body.bytesize % 512
  end

  test "stable release tags, whose names hold dots, route to the proxy" do
    route = Rails.application.routes.recognize_path("/releases/v0.1.0/skyblip-simulator.wasm", method: :get)

    assert_equal "releases/assets", route.fetch(:controller)
    assert_equal "v0.1.0", route.fetch(:release_tag)
    assert_equal "skyblip-simulator.wasm", route.fetch(:file)
  end

  test "only release tags and approved file names can be fetched" do
    [ "/releases/main/config.yml", "/releases/dev-1005/console.html", "/releases/dev-1005/evil.js" ].each do |path|
      get path
      assert_response :not_found
    end
  end

  # Each download holds a thread while GitHub answers, so a name no release holds must cost no fetch.
  test "a tag or a file no published release holds is refused without a download" do
    VCR.use_cassette("github/releases", allow_playback_repeats: true) do
      [ "/releases/dev-9999/skyblip-go-dev-9999.signed.bin", "/releases/dev-1765/skyblip-go-dev-1764.signed.bin" ].each do |path|
        get path
        assert_response :not_found
      end
    end
  end
end
