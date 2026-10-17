require "test_helper"
require "tmpdir"
require "zlib"

class Content::SitemapTest < ActiveSupport::TestCase
  test "the sitemap lists the content collection with translated and nested URLs" do
    sitemap = Content::Sitemap.new
    document = Nokogiri::XML(sitemap.to_xml)
    urls = document.xpath("//xmlns:loc").map(&:text)

    assert_equal Content::Page.all.map { |page| Content.canonical_url(page.url_path) }.sort, urls
    assert_includes urls, "https://skyblip.eu/fr/a-propos"
    assert_includes urls, "https://skyblip.eu/ads-l-registry/nmea"
    assert urls.none? { |url| url.include?("/briefing") }
    assert_empty document.xpath("//xmlns:lastmod")
  end

  test "entry URLs use translated slugs" do
    page = Content::Page.find("a-propos", locale: :fr)
    entry = Content::Sitemap::Entry.new(page)

    assert_equal "https://skyblip.eu/fr/a-propos", entry.canonical_url
  end

  test "sitemap output stays current when checkout timestamps change" do
    page = Content::Page.find("a-propos", locale: :fr)
    source_stat = page.path.stat

    begin
      Dir.mktmpdir do |directory|
        sitemap = Content::Sitemap.new(build_dir: directory, generate_robots: true)
        File.utime(Time.utc(2000, 1, 1), Time.utc(2000, 1, 1), page.path)
        sitemap.generate
        original_xml = sitemap.to_xml
        original_gzip = File.binread(File.join(directory, "sitemap.xml.gz"))

        File.utime(Time.utc(2001, 1, 1), Time.utc(2001, 1, 1), page.path)

        assert_equal original_xml, sitemap.to_xml
        assert sitemap.current?
        sitemap.generate
        assert_equal original_gzip, File.binread(File.join(directory, "sitemap.xml.gz"))
      end
    ensure
      File.utime(source_stat.atime, source_stat.mtime, page.path)
    end
  end

  test "generation writes matching XML gzip and robots files and detects outdated output" do
    Dir.mktmpdir do |directory|
      sitemap = Content::Sitemap.new(build_dir: directory, generate_robots: true)

      assert_not sitemap.current?
      assert_equal Content::Page.all.size, sitemap.generate
      assert sitemap.current?
      assert_equal sitemap.to_xml, Zlib::GzipReader.open(File.join(directory, "sitemap.xml.gz"), &:read)
      assert_includes File.read(File.join(directory, "robots.txt")), "Sitemap: https://skyblip.eu/sitemap.xml\n"

      File.write(File.join(directory, "sitemap.xml"), "outdated")

      assert_not sitemap.current?
    end
  end

  test "invalid sitemap configuration fails before writing output" do
    sitemap = Content::Sitemap.new(base_url: "not a URL")

    assert_raises(ActiveModel::ValidationError) { sitemap.generate }
  end
end
