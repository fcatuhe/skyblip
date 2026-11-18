require "test_helper"

class Content::BuyTest < ActionDispatch::IntegrationTest
  test "the buy page opens on the language's country and gives every country a recommended and an alternative offer" do
    { "/buy" => "gb", "/fr/s-equiper" => "fr" }.each do |path, country|
      get path

      assert_select "#where .country-tabs input[checked][value=?]", country
      css_select("#where .country-tabs input").each do |tab|
        assert_select "#where table.offers tbody[data-country=?]", tab["value"], count: 2
      end
    end
  end

  test "Bravo and the offers shipped from China stay outside the country tabs" do
    get "/buy"

    assert_select "#where table.offers:not(.offers--other) tbody:not([data-country]) th", text: /Bravo/
    assert_select "#from-china ~ .table-scroll tbody:not([data-country]) a[href^='https://www.lilygo.cc']", count: 2
  end
end
