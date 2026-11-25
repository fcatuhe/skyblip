require "test_helper"

class TrailingSlashTest < ActionDispatch::IntegrationTest
  test "a path with a trailing slash redirects permanently to the path without it" do
    get "/fr/"
    assert_response :moved_permanently
    assert_redirected_to "/fr"

    get "/fr/a-propos/?ref=flyer"
    assert_response :moved_permanently
    assert_redirected_to "/fr/a-propos?ref=flyer"
  end

  test "a path with extra leading slashes redirects on this site, not to the host they spell" do
    get "//example.com/"
    assert_response :moved_permanently
    assert_redirected_to "http://www.example.com/example.com"

    get "///example.com/?ref=flyer"
    assert_redirected_to "http://www.example.com/example.com?ref=flyer"
  end

  test "the root and a slash inside the query string do not redirect" do
    get "/"
    assert_response :success

    get "/fr/a-propos?ref=/flyer/"
    assert_response :success
  end
end
