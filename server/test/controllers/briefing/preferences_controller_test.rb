require "test_helper"

class Briefing::PreferencesControllerTest < ActionDispatch::IntegrationTest
  test "the settings offer each language in its own words, the current one chosen" do
    get briefing_preferences_path

    assert_response :success
    assert_select "input[type=radio][name='preferences[locale]'][value=en][checked]"
    assert_select "label[lang=fr]", text: "Français"
    assert_select "main script, main style, main [style], main [onclick]", count: 0
  end

  test "choosing a language keeps it for the whole app, at the same addresses" do
    patch briefing_preferences_path, params: { preferences: { locale: "fr" } }

    assert_redirected_to briefing_preferences_path
    assert_equal "fr", cookies[:locale]

    get briefing_root_path
    assert_select "html[lang=fr]"
    assert_select "h1", text: "Votre skyBlip"
  end

  test "development builds stay off until asked for, and the language stays as it was" do
    cookies[:locale] = "fr"
    get briefing_preferences_path
    assert_select "input[type=checkbox][name='preferences[development_builds]']:not([checked])"
    assert_select "a[href$='/releases']", count: 0

    patch briefing_preferences_path, params: { preferences: { development_builds: "1" } }

    assert_equal "true", cookies[:development_builds]
    assert_equal "fr", cookies[:locale]
    follow_redirect!
    assert_select "input[type=checkbox][name='preferences[development_builds]'][checked]"
    assert_select "a[href='#{Rails.configuration.x.repository_url}/releases']"
  end

  test "a language the app does not speak is refused" do
    patch briefing_preferences_path, params: { preferences: { locale: "nl" } }

    assert_response :unprocessable_entity
    assert_nil cookies[:locale]
  end

  test "before any choice the app speaks the browser's language, and a choice wins over it" do
    get briefing_root_path, headers: { "Accept-Language" => "nl-NL,fr-FR;q=0.8,en;q=0.5" }
    assert_select "html[lang=fr]"

    cookies[:locale] = "en"
    get briefing_root_path, headers: { "Accept-Language" => "fr-FR" }
    assert_select "html[lang=en]"
  end

  # The public site picks its language from the address, and its simulator is a briefing frame.
  test "the simulator a French public page embeds speaks French" do
    get "/fr/essayer"
    frame = css_select("turbo-frame#simulator_build").first
    assert_equal briefing_simulator_build_path(locale: :fr), frame["src"]

    VCR.use_cassette("github/releases") { get frame["src"] }
    assert_select "html[lang=fr]"
  end
end
