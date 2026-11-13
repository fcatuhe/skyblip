require "test_helper"

class StylesheetsTest < ActionDispatch::IntegrationTest
  test "a public page links the site's stylesheets and only the areas it uses" do
    areas_by_page = { "skyblip-go" => %w[simulator] }

    Content::Page.all.each do |page|
      get page.url_path

      assert_equal areas_by_page.fetch(page.base_slug, []), linked_areas, page.path
      assert_includes stylesheets, "_global.css", page.path
    end
  end

  test "the admin, the sign-in page and the briefing pages link their own areas" do
    get "/session/new"
    assert_equal %w[admin], linked_areas

    get "/briefing/simulator"
    assert_equal %w[simulator workbench briefing], linked_areas

    get "/briefing/console"
    assert_equal %w[workbench briefing], linked_areas

    get "/briefing/ownship"
    assert_equal %w[ownship briefing], linked_areas

    %w[/briefing /briefing/activate /briefing/tools /briefing/devices/go-00012a /briefing/devices/go-00012a/firmware].each do |path|
      get path
      assert_equal %w[briefing], linked_areas, path
    end

    sign_in_as users(:francois)
    get "/admin/users"
    assert_equal %w[admin], linked_areas
  end

  test "the frames the briefing pages load link their areas when opened on their own" do
    VCR.use_cassette("github/releases", allow_playback_repeats: true) do
      { "/briefing/firmwares" => %w[briefing],
        "/briefing/simulator_build" => %w[simulator briefing],
        "/briefing/simulator_build?mode=workbench" => %w[simulator workbench briefing] }.each do |path, areas|
        get path

        assert_response :success, path
        assert_equal areas, linked_areas, path
      end
    end
  end

  test "the layer order is declared before any stylesheet that uses a layer" do
    get "/briefing/simulator"

    assert_equal "_global.css", stylesheets.first
  end

  private
    def stylesheets
      css_select("link[rel=stylesheet]").map { |link| link["href"].delete_prefix("/assets/").sub(/-\h{8}\.css\z/, ".css") }
    end

    def linked_areas
      stylesheets.map { |path| File.dirname(path) }.uniq.without(".")
    end
end
