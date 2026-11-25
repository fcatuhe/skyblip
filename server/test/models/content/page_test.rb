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

  test "a page's Markdown lives at its address with .md, and a homepage's at index.md" do
    assert_equal "/index.md", Content::Page.find("index", locale: :en).markdown_path
    assert_equal "/fr/index.md", Content::Page.find("index", locale: :fr).markdown_path
    assert_equal "/fr/a-propos.md", Content::Page.find("a-propos", locale: :fr).markdown_path
    assert_equal "/ads-l/nmea.md", Content::Page.find("ads-l/nmea", locale: :en).markdown_path
  end

  test "a page is found at its address or at its Markdown's, with or without a trailing slash" do
    assert_equal "/fr/a-propos", Content::Page.at("/fr/a-propos/").url_path
    assert_equal "/fr/a-propos", Content::Page.at("/fr/a-propos.md").url_path
    assert_equal "/", Content::Page.at("").url_path
    assert_equal "/fr", Content::Page.at("/fr/index.md").url_path
    assert_nil Content::Page.at("/fr/about")
  end

  test "a reader in a language gets every page in it, and in English the pages not translated yet" do
    french = Content::Page.read_in(:fr)

    assert_equal Content::Page.read_in(:en).map(&:base_slug).sort, french.map(&:base_slug).sort
    assert_equal :fr, french.find { it.base_slug == "about" }.locale
    assert_equal :en, french.find { it.base_slug == "ads-l/nmea" }.locale
  end

  test "nested pages retain their URL and source path" do
    page = Content::Page.find("ads-l/nmea", locale: :en)

    assert_equal "/ads-l/nmea", page.url_path
    assert_equal Rails.root.join("content/pages/ads-l/nmea.html.erb"), page.path.expand_path
    assert_nil Content::Page.find_by(base_slug: page.base_slug, locale: :fr)
  end

  test "a locale never borrows another locale's URL slug" do
    assert_raises(Decant::FileNotFound) { Content::Page.find("about", locale: :fr) }
    assert_raises(Decant::FileNotFound) { Content::Page.find("a-propos", locale: :en) }
  end

  test "every page says when it last changed, and an article was published before that" do
    Content::Page.all.each do |page|
      assert_kind_of Date, page.updated_on, page.path
      assert_operator page.published_on, :<=, page.updated_on, page.path if page.article?
    end
  end

  test "a page with a publication date is an article, and a guide names its parent" do
    assert Content::Page.find("see-and-avoid", locale: :en).article?
    assert_not Content::Page.find("radar", locale: :en).article?

    guide = Content::Page.find("radar", locale: :fr)
    assert_equal "/fr/guides", guide.parent.url_path
    assert_nil Content::Page.find("guides", locale: :fr).parent
  end

  test "blog posts read oldest first, in their own language" do
    posts = Content::Page.find_by(base_slug: "blog/club-fleet", locale: :fr).reading_order

    assert_equal posts.map(&:published_on).sort, posts.map(&:published_on)
    assert_equal "blog/why-ads-l", posts.first.base_slug
    assert_equal [ :fr ], posts.map(&:locale).uniq
  end

  test "guides read in the order the guides index lists them" do
    radar = Content::Page.find("radar", locale: :en)

    assert_equal %w[mount go-first-steps bravo-first-steps radar instruments displays controls power phone radio timing gps traffic], radar.reading_order.map(&:base_slug)
    assert_equal 4, radar.reading_position
    assert_equal "bravo-first-steps", radar.previous_page.base_slug
    assert_equal "instruments", radar.next_page.base_slug
  end

  test "the first page of a section has nothing before it, and the last nothing after it" do
    assert_nil Content::Page.find("ads-l/registry", locale: :en).previous_page
    assert_nil Content::Page.find("traffic", locale: :en).next_page
  end
end
