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

  # Without a maskable icon, Android shrinks ours into a white circle on the home screen.
  test "the app gives Android a full-bleed icon to crop" do
    get briefing_manifest_path(format: :json)

    maskable = response.parsed_body["icons"].select { it["purpose"] == "maskable" }
    assert_equal %w[/icon-maskable.png /icon-maskable.svg], maskable.pluck("src")
    maskable.each { assert Rails.public_path.join(it["src"].delete_prefix("/")).file?, it["src"] }
  end

  test "no public page offers the app" do
    Content::Page.all.each do |page|
      get page.url_path
      assert_select "link[rel=manifest]", { count: 0 }, page.path
    end
  end
end
