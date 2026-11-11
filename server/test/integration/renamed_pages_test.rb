require "test_helper"

class RenamedPagesTest < ActionDispatch::IntegrationTest
  test "the update page's old addresses redirect permanently to its new ones" do
    get "/manage"
    assert_response :moved_permanently
    assert_redirected_to "/update"

    get "/fr/mise-a-jour"
    assert_response :moved_permanently
    assert_redirected_to "/fr/mettre-a-jour"
  end

  test "the update page answers at its new addresses, each linking the other language" do
    get "/update"
    assert_response :success
    assert_select ".locale-switcher a[href='/fr/mettre-a-jour']", text: "FR"

    get "/fr/mettre-a-jour"
    assert_response :success
    assert_select ".locale-switcher a[href='/update']", text: "EN"
  end
end
