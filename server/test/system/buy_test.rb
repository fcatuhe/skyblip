require "application_system_test_case"

class BuyTest < ApplicationSystemTestCase
  test "a country tab shows that country's offers only, and keeps Bravo and China" do
    visit "/buy"

    within "#where" do
      assert_link "£52"
      assert_no_link "€75"

      find("label", text: "Spain").click

      assert_link "€75"
      assert_no_link "£52"
      assert_link "$55.95"
      assert_link "$59.84"
    end
  end
end
