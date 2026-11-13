require "test_helper"

class Briefing::ManifestsControllerTest < ActionDispatch::IntegrationTest
  test "the app installs from the briefing, and stays inside it" do
    get briefing_manifest_path(format: :json)

    manifest = response.parsed_body
    assert_equal "skyBlip Briefing", manifest["name"]
    assert_equal "/briefing", manifest["start_url"]
    assert_equal "/briefing", manifest["scope"]
    assert_equal "standalone", manifest["display"]
  end

  test "the app installed in French starts in French, at the same address" do
    cookies[:locale] = "fr"
    get briefing_manifest_path(format: :json)

    manifest = response.parsed_body
    assert_equal "/briefing", manifest["start_url"]
    assert_equal "/briefing", manifest["scope"]
    assert_equal "fr", manifest["lang"]
  end

  test "no public page offers the app" do
    Content::Page.all.each do |page|
      get page.url_path
      assert_select "link[rel=manifest]", { count: 0 }, page.path
    end
  end
end
