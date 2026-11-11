require "test_helper"

class Briefing::DashboardsControllerTest < ActionDispatch::IntegrationTest
  test "briefing links the device tools without calling GitHub" do
    get "/briefing"

    assert_response :success
    assert_select "h1", "Briefing"
    assert_select "a[href='/activate']"
    assert_select "a[href='/briefing/simulator']"
    assert_select "a[href='/briefing/console']"
    assert_select "a[href='/briefing/ownship']"
    assert_select "a[href='/update']"
    assert_select "a[href='/configure']"
  end

  test "French briefing links the French pages and switches back to English" do
    get "/fr/briefing"

    assert_response :success
    assert_select "a[href='/fr/activer']"
    assert_select "a[href='/fr/mettre-a-jour']"
    assert_select "a[href='/fr/regler']"
    assert_select ".locale-switcher a[href='/briefing']", text: "EN"
    assert_select "link[rel=alternate][hreflang=en][href=?]", "https://skyblip.eu/briefing"
  end

  test "English briefing offers French" do
    get "/briefing"

    assert_select ".locale-switcher a[href='/fr/briefing']", text: "FR"
    assert_select "link[rel=alternate][hreflang=fr][href=?]", "https://skyblip.eu/fr/briefing"
  end
end
