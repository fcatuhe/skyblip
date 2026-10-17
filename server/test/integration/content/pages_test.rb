require "test_helper"

class Content::PagesTest < ActionDispatch::IntegrationTest
  test "every published content URL renders through the content controller" do
    Content::Page.all.each do |page|
      get page.url_path

      assert_response :success, page.url_path
      assert_equal "content/pages", request.path_parameters[:controller]
      assert_select "html[lang=?]", page.locale.to_s
      assert_select "link[rel=canonical][href=?]", Content.canonical_url(page.url_path)
    end
  end

  test "translated pages link to their translated and default canonical URLs" do
    get "/fr/a-propos", headers: { "Host" => "preview.example.com" }

    assert_response :success
    assert_select "link[rel=canonical][href=?]", "https://skyblip.eu/fr/a-propos"
    assert_select "link[rel=alternate][hreflang=en][href=?]", "https://skyblip.eu/about"
    assert_select "link[rel=alternate][hreflang=x-default][href=?]", "https://skyblip.eu/about"
    assert_select ".locale-switcher a[href='/about']", text: "EN"
  end

  test "untranslated content does not advertise a missing French alternate" do
    get "/ads-l-registry/nmea"

    assert_response :success
    assert_select "link[rel=alternate][hreflang=fr]", count: 0
    assert_select ".locale-switcher [aria-disabled=true]", text: "FR"
  end

  test "unknown and noncanonical content slugs are not recognized" do
    %w[/unknown /index /fr/index /fr/about /a-propos /fr/ads-l-registry/nmea /fr/fr /fr/fr/a-propos].each do |path|
      assert_raises(ActionController::RoutingError, path) do
        Rails.application.routes.recognize_path(path, method: :get)
      end
    end
  end

  test "application routes never resolve to the content controller" do
    %w[/briefing /briefing/login /fr/briefing /fr/briefing/console].each do |path|
      route = begin
        Rails.application.routes.recognize_path(path, method: :get)
      rescue ActionController::RoutingError
        nil
      end

      assert_not_equal "content/pages", route&.fetch(:controller), path
    end
  end
end
