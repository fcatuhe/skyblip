require "application_system_test_case"

class LocaleTest < ApplicationSystemTestCase
  test "a first visit takes the browser's language, and keeps it over the language of the link" do
    visit "/fr/a-propos"
    assert_current_path "/about"

    visit "/fr/a-propos"
    assert_current_path "/about"
  end

  test "a French browser opening an English link gets French" do
    speak "fr-FR"
    visit "/about"

    assert_current_path "/fr/a-propos"
  end

  test "a language chosen in the switcher wins over the browser's" do
    visit "/about"
    within(".locale-switcher") { click_link "FR" }
    assert_current_path "/fr/a-propos"

    visit "/about"
    assert_current_path "/fr/a-propos"
  end

  private
    def speak(language)
      page.driver.browser.page.command("Emulation.setUserAgentOverride", userAgent: page.driver.browser.default_user_agent, acceptLanguage: language)
    end
end
