require "test_helper"

class WwwTest < ActionDispatch::IntegrationTest
  test "the www host redirects permanently to the same URL on skyblip.eu" do
    get "/", headers: { "Host" => "www.skyblip.eu" }
    assert_response :moved_permanently
    assert_redirected_to "https://skyblip.eu/"

    get "/fr/a-propos?ref=flyer", headers: { "Host" => "www.skyblip.eu" }
    assert_response :moved_permanently
    assert_redirected_to "https://skyblip.eu/fr/a-propos?ref=flyer"
  end

  test "the www host redirects files served from public too" do
    get "/sitemap.xml", headers: { "Host" => "www.skyblip.eu" }
    assert_response :moved_permanently
    assert_redirected_to "https://skyblip.eu/sitemap.xml"
  end

  test "the canonical host still serves files from public" do
    get "/sitemap.xml", headers: { "Host" => "skyblip.eu" }
    assert_response :ok
  end
end
