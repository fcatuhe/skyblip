require "test_helper"

class Content::JourneyTest < ActionDispatch::IntegrationTest
  test "the site menu and footer lead to the same stages in both languages" do
    { "/fr" => [ %w[/fr/essayer /fr/go /fr/s-equiper /fr/installer /fr/guides], [ "Essayer", "Appareils", "S'équiper", "Installer", "Guides" ] ],
      "/" => [ %w[/try /go /buy /install /guides], [ "Try it", "Devices", "Buy", "Install", "Guides" ] ] }.each do |path, (stages, labels)|
      get path

      links = css_select(".site-nav-links a")
      assert_equal stages, links.map { |link| link["href"] }
      assert_equal labels, links.map(&:text)
      assert_equal stages, css_select(".site-footer-columns a").map { |link| link["href"] }.intersection(stages)
    end
  end

  test "each device page links to all three devices in the sticky header and selects itself" do
    [ "", "/fr" ].each do |prefix|
      paths = %w[go bravo tango].map { |device| "#{prefix}/#{device}" }
      paths.each do |path|
        get path

        assert_equal paths, css_select(".device-tabs a").map { |link| link["href"] }
        assert_select ".device-tabs a[aria-current=page]", count: 1
        assert_select ".device-tabs a[aria-current=page][href=?]", path
        assert_select ".site-header > .site-nav + .device-tabs"
      end
    end
  end

  test "the home simulator loads lazily after the radar explanation and links to the dedicated simulator" do
    { "/fr" => "/fr/essayer", "/" => "/try" }.each do |home, trial|
      get home

      assert_select "#where-to-look + #simulator turbo-frame#simulator_build[loading=lazy][src=?]",
        briefing_simulator_build_path(locale: home == "/fr" ? "fr" : "en")
      assert_select "#simulator a[href=?]", trial

      get trial
      assert_select "#simulator turbo-frame#simulator_build[loading=eager]"
    end
  end

  test "the localized installation pages lead to Briefing and keep the manual instructions addressable" do
    { "/fr/installer" => [ "fr", "/install" ], "/install" => [ "en", "/fr/installer" ] }.each do |path, (locale, translation)|
      get path

      assert_response :success
      assert_select "link[rel=alternate][href=?]", Content.canonical_url(translation)
      assert_select "main a[href=?]", briefing_setup_path(locale:)
      assert_no_match "CURRENT.UF2", response.body
      assert_select "#si-quelque-chose-bloque a[href=?]", briefing_activation_path(locale:)
      assert_select "#si-quelque-chose-bloque #transponder"

      get briefing_setup_path(locale:)
      assert_select "[data-panel=unsupported] a[href=?]", "#{path}#si-quelque-chose-bloque"
    end
  end

  test "old product and purchase links still land at their sections" do
    [ "", "/fr" ].each do |prefix|
      get "#{prefix}/#{prefix.empty? ? 'buy' : 's-equiper'}"
      assert_select "#where, #compare, #install, #install-bravo, #type", count: 5
      assert_select "#install a[href=?]", prefix.empty? ? "/install" : "/fr/installer"

      get "#{prefix}/bravo"
      assert_select "#status, #install, #buy", count: 3

      get "#{prefix}/tango"
      assert_select "#buy"

      get "#{prefix}/go"
      assert_select "a#controls[href=?]", prefix.empty? ? "/controls#two-contacts" : "/fr/commandes#two-contacts"
      assert_select "a#power[href=?]", prefix.empty? ? "/power" : "/fr/alimentation"

      get prefix.empty? ? "/roadmap" : "/fr/a-venir"
      assert_select "a#bravo[href=?]", "#{prefix}/bravo"
      assert_select "a#tango[href=?]", "#{prefix}/tango"
    end
  end
end
