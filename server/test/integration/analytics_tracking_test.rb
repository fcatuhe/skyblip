require "test_helper"
require_relative "../test_helpers/analytics_test_helper"

class AnalyticsTrackingTest < ActionDispatch::IntegrationTest
  include AnalyticsTestHelper

  SAFARI = "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15"

  # Development and test turn tracking off, so this is the one place the production path runs.
  setup do
    clear_analytics
    Ahoy.api_only = false
  end

  teardown { Ahoy.api_only = true }

  test "a page served and viewed in a browser becomes a computed visit" do
    perform_enqueued_jobs do
      get page_path(slug: "go"), headers: { "User-Agent" => SAFARI }
      post "/hoy/events", params: { events: [ { name: "$view", properties: { page: "/features" }, time: Time.current.to_f } ] }.to_json,
                          headers: { "User-Agent" => SAFARI, "Content-Type" => "application/json",
                                     "Ahoy-Visit" => cookies[:ahoy_visit], "Ahoy-Visitor" => cookies[:ahoy_visitor] }
    end

    visit = Analytics::Visit.sole
    assert_equal [ "/features" ], visit.page_paths
    assert visit.non_user?
    assert visit.section_public?
    assert_equal "content/pages", visit.ahoy_visit.events.find_by!(name: "pages#show").properties["controller"]
  end

  test "a visit keeps the browser, system and device the agent names, and neither the agent nor the address" do
    get page_path(slug: "go"), headers: { "User-Agent" => SAFARI, "REMOTE_ADDR" => "203.0.113.8" }

    visit = Ahoy::Visit.sole
    assert_equal [ "Safari", "Mac", "Desktop" ], [ visit.browser, visit.os, visit.device_type ]
    assert_equal [ nil, nil ], [ visit.ip, visit.user_agent ]
  end

  test "the tracking script loads only where tracking is on" do
    get root_path
    assert_select "script[type=module]", text: /ahoy-module/

    Ahoy.api_only = true
    get root_path
    assert_select "script[type=module]", text: /ahoy-module/, count: 0
  end

  test "a turbo prefetch is not a visit to the page" do
    get page_path(slug: "go"), headers: { "User-Agent" => SAFARI, "X-Sec-Purpose" => "prefetch" }

    assert_empty Ahoy::Event.all
  end

  test "an event whose page is not a path is dropped" do
    get page_path(slug: "go"), headers: { "User-Agent" => SAFARI }
    post "/hoy/events", params: { events: [ { name: "$view", properties: { page: [ "/features" ] } }, { name: "$click", properties: "text" } ] }.to_json,
                        headers: { "User-Agent" => SAFARI, "Content-Type" => "application/json",
                                   "Ahoy-Visit" => cookies[:ahoy_visit], "Ahoy-Visitor" => cookies[:ahoy_visitor] }

    assert_equal [ "pages#show" ], Ahoy::Event.pluck(:name)
  end

  # A response public caches keep a year must not hand one visitor's cookie to the next.
  test "a firmware download sets no cookie" do
    VCR.use_cassette("github/firmware") do
      get "/releases/dev-1005/skyblip-go-dev-1005.signed.bin", headers: { "User-Agent" => SAFARI }
    end

    assert_response :success
    assert_includes response.headers["Cache-Control"], "public"
    assert_nil response.headers["Set-Cookie"]
  end

  test "the visitor cookie lives 13 months from the latest visit" do
    get page_path(slug: "go"), headers: { "User-Agent" => SAFARI }
    visitor = cookies[:ahoy_visitor]
    assert_match(/ahoy_visitor=#{visitor};.*expires=#{13.months.from_now.utc.strftime("%a, %d %b %Y")}/i, response.headers["Set-Cookie"].to_s)

    travel 1.month do
      cookies.delete(:ahoy_visit)
      get page_path(slug: "about"), headers: { "User-Agent" => SAFARI }
      assert_match(/ahoy_visitor=#{visitor};.*expires=#{13.months.from_now.utc.strftime("%a, %d %b %Y")}/i, response.headers["Set-Cookie"].to_s)
    end
  end
end
