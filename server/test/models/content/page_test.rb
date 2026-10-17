require "test_helper"

class Content::PageTest < ActiveSupport::TestCase
  test "translated slugs keep the identity of their source page" do
    page = Content::Page.find("a-propos", locale: :fr)

    assert_equal "about", page.base_slug
    assert_equal "a-propos", page.to_param
    assert_equal "/fr/a-propos", page.url_path
    assert_equal "about", Content::Page.find_by(base_slug: page.base_slug, locale: :en).to_param
  end

  test "homepages retain their locale root URLs" do
    assert_equal "/", Content::Page.find("index", locale: :en).url_path
    assert_equal "/fr", Content::Page.find("index", locale: :fr).url_path
  end

  test "nested pages retain their URL and source path" do
    page = Content::Page.find("ads-l-registry/nmea", locale: :en)

    assert_equal "/ads-l-registry/nmea", page.url_path
    assert_equal Rails.root.join("content/pages/ads-l-registry/nmea.html.erb"), page.path.expand_path
    assert_nil Content::Page.find_by(base_slug: page.base_slug, locale: :fr)
  end

  test "a locale never borrows another locale's URL slug" do
    assert_raises(Decant::FileNotFound) { Content::Page.find("about", locale: :fr) }
    assert_raises(Decant::FileNotFound) { Content::Page.find("a-propos", locale: :en) }
  end
end
