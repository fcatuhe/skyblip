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
end
