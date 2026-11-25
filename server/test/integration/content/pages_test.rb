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
    assert_select ".locale-switcher summary", text: "FR"
    assert_select ".locale-switcher a[href='/about'][hreflang=en]", text: "English"
  end

  test "the language switcher lists the browser's languages first, in its order, then the others by name" do
    get "/about", headers: { "Accept-Language" => "it-IT,es;q=0.5,fr;q=0.9,xx;q=0.8" }

    assert_equal %w[it fr es de en], css_select(".locale-switcher li").map { it["lang"] }
  end

  test "the footer offers the contact email first, then the repository on GitHub" do
    get "/fr/a-propos"

    assert_select ".site-footer-contact", text: /Écrivez-nous à contact@skyblip\.eu ou sur GitHub\./
    assert_select ".site-footer-contact a" do |links|
      assert_equal [ "mailto:contact@skyblip.eu", "https://github.com/fcatuhe/skyblip" ], links.map { it["href"] }
    end
  end

  test "untranslated content does not advertise a missing French alternate" do
    get "/ads-l/nmea"

    assert_response :success
    assert_select "link[rel=alternate][hreflang=fr]", count: 0
    assert_select ".locale-switcher [aria-disabled=true]", text: "Français"
  end

  # A request for /?locale=zz answered a 500, since I18n refuses a locale it does not know.
  test "a locale the site does not speak, asked in the query, is ignored" do
    get "/about", params: { locale: "zz" }

    assert_response :success
    assert_select "html[lang=en]"
  end

  test "unknown and noncanonical content slugs are not recognized" do
    %w[/unknown /index /fr/index /fr/about /a-propos /fr/ads-l/nmea /fr/fr /fr/fr/a-propos].each do |path|
      assert_raises(ActionController::RoutingError, path) do
        Rails.application.routes.recognize_path(path, method: :get)
      end
    end
  end

  test "every page in a section links the pages before and after it in its reading order" do
    Content::Page.all.select(&:parent).each do |page|
      get page.url_path

      assert_equal 1, css_select("nav.pager").size, page.url_path
      assert_equal [ page.previous_page&.url_path ].compact, css_select("nav.pager a[rel=prev]").map { it["href"] }, page.url_path
      assert_equal [ page.next_page&.url_path ].compact, css_select("nav.pager a[rel=next]").map { it["href"] }, page.url_path
    end
  end

  test "a blog post offers the next post on the right, before its sources" do
    get "/fr/blog/flotte-de-club"

    assert_select ".pager-place", text: "Blog · 5 sur 8"
    assert_select ".pager-link--previous[href='/fr/blog/reseau-ou-radio']", text: /Précédent/
    assert_select ".pager-link--next[href='/fr/blog/poche-ou-installe']", text: /Suivant/

    get "/blog/who-sees-ads-l"

    assert_equal %w[nav ul], css_select("nav.pager, ul.sources").map(&:name)
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

  # Google runs the page's script with an English browser and no cookie, and must still index the French pages.
  test "a crawler gets no language redirect, a browser does" do
    get "/fr/a-propos", headers: { "User-Agent" => "Mozilla/5.0 AppleWebKit/537.36 (KHTML, like Gecko; compatible; Googlebot/2.1; +http://www.google.com/bot.html) Chrome/120.0.0.0 Safari/537.36" }
    assert_select ".locale-switcher:not([data-controller])"

    get "/fr/a-propos", headers: { "User-Agent" => "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36" }
    assert_select ".locale-switcher[data-controller=locale]"
  end
end
