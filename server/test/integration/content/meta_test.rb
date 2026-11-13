require "test_helper"

class Content::MetaTest < ActionDispatch::IntegrationTest
  test "every page fits its title and description in a search result and shares a 1200x630 image" do
    Content::Page.all.each do |page|
      get page.url_path

      title = css_select("head > title").text
      assert_operator title.length, :<=, 60, page.path
      assert_includes title, "skyBlip", page.path
      assert_operator css_select("meta[name=description]").first["content"].length, :<=, 155, page.path
      assert_select "meta[property='og:image'][content^='https://skyblip.eu/assets/share/']", 1, page.path
      assert_select "meta[property='og:image:width'][content='1200']"
      assert_select "meta[name='twitter:card'][content=summary_large_image]"
      assert_includes structured_data_types, "WebPage", page.path
    end
  end

  test "a page shares the card it names" do
    get "/fr/bravo"
    assert_select "meta[property='og:image'][content*='/share/bravo-']"
    assert_select "meta[property='og:locale'][content=fr_FR]"
    assert_select "meta[property='og:locale:alternate'][content=en_GB]"

    get "/radar"
    assert_select "meta[property='og:image'][content*='/share/radar-']"

    get "/ads-l-registry/nmea"
    assert_select "meta[property='og:image'][content*='/share/go-']"
    assert_select "meta[property='og:locale:alternate']", 0
  end

  test "the homepage describes the organization and the site" do
    get "/fr"

    assert_equal "ADS-L et visibilité électronique pour pilotes · skyBlip", css_select("head > title").text
    assert_equal "https://github.com/fcatuhe/skyblip", structured_data("Organization")["sameAs"].first
    assert_equal %w[en fr], structured_data("WebSite")["inLanguage"]
  end

  test "a guide carries its breadcrumb through the guides index" do
    get "/fr/radar"

    crumbs = structured_data("BreadcrumbList")["itemListElement"]
    assert_equal [ "https://skyblip.eu/fr", "https://skyblip.eu/fr/guides", "https://skyblip.eu/fr/radar" ], crumbs.pluck("item")
    assert_equal [ 1, 2, 3 ], crumbs.pluck("position")
  end

  test "the Go and Bravo are marked up as products, with the lowest price each page shows" do
    get "/go"
    offer = structured_data("Product")["offers"]
    assert_equal [ "AggregateOffer", "68", "EUR" ], offer.values_at("@type", "lowPrice", "priceCurrency")
    assert_equal "https://skyblip.eu/buy", offer["url"]

    get "/bravo"
    offer = structured_data("Product")["offers"]
    assert_equal [ "AggregateOffer", "44.91", "USD" ], offer.values_at("@type", "lowPrice", "priceCurrency")

    %w[/tango /buy /].each do |path|
      get path
      assert_not_includes structured_data_types, "Product", path
    end
  end

  test "a long-form page is an article with its publication date" do
    get "/see-and-avoid"

    article = structured_data("Article")
    assert_equal [ "2026-09-23", "François Catuhe" ], [ article["datePublished"], article.dig("author", "name") ]
    assert_select "meta[property='og:type'][content=article]"
  end

  test "an application page takes the title its view gives and no structured data" do
    get "/briefing"

    assert_equal "Briefing · skyBlip", css_select("head > title").text
    assert_select "script[type='application/ld+json']", 0
  end

  private
    def structured_data_graph
      JSON.parse(css_select("script[type='application/ld+json']").first.text).fetch("@graph")
    end

    def structured_data_types = structured_data_graph.pluck("@type")

    def structured_data(type) = structured_data_graph.find { |node| node["@type"] == type }
end
