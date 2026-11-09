require "application_system_test_case"

class PromoCodeTest < ApplicationSystemTestCase
  test "the Seeed promo code copies to the clipboard in one click" do
    page.driver.browser.command("Browser.grantPermissions", permissions: %w[clipboardReadWrite clipboardSanitizedWrite])
    visit "/bravo"

    within ".offer-price" do
      assert_no_selector ".promo-code--copied"
      click_button "5GO3A5DD"
      assert_selector ".promo-code--copied", text: "copied"
    end
  end
end
