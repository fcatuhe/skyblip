require "application_system_test_case"

class NavTest < ApplicationSystemTestCase
  test "on a phone the site menu slides in from a button, and closes on its button, on Escape or on the page behind it" do
    page.current_window.resize_to(390, 800)
    visit "/blog"

    assert_no_selector ".site-nav > .site-nav-links a"
    open_menu
    within(".site-nav-panel") { assert_link "Guides" }

    find(:button, "Close the menu", enable_aria_label: true).click
    assert_no_selector ".site-nav-panel[open]"

    open_menu
    find(".site-nav-panel").send_keys(:escape)
    assert_no_selector ".site-nav-panel[open]"

    open_menu
    page.driver.browser.mouse.click(x: 20, y: 400)
    assert_no_selector ".site-nav-panel[open]"

    open_menu
    within(".site-nav-panel") { click_link "Guides" }
    assert_current_path "/guides"
    assert_no_selector ".site-nav-panel[open]"
  ensure
    page.current_window.resize_to(*ApplicationSystemTestCase::SCREEN_SIZE)
  end

  test "on a wide screen the links sit in the bar and the menu button is hidden" do
    visit "/blog"

    within(".site-nav > .site-nav-links") { assert_link "Guides" }
    assert_no_selector :button, "Open the menu", enable_aria_label: true
  end

  private
    def open_menu
      find(:button, "Open the menu", enable_aria_label: true).click
      assert_selector ".site-nav-panel[open]"
    end
end
