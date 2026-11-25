require "application_system_test_case"

class AdsLQuestionsTest < ApplicationSystemTestCase
  test "the list shows every question collapsed, and a question's reference opens it" do
    visit "/ads-l/questions"

    assert_selector ".questions details", count: 11
    assert_no_selector ".questions details[open]"

    click_link "SB-Q4"
    assert_selector "details#sb-q4[open]"
    assert_selector "details#sb-q4 figure.diagram", count: 2
  end

  test "a link to a question's reference opens it on arrival" do
    visit "/ads-l/questions#sb-q6"

    assert_selector "details#sb-q6[open]"
    assert_selector ".questions details[open]", count: 1
  end
end
