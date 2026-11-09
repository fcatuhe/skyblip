class Content::Sitemap::Entry
  attr_reader :page, :base_url

  def initialize(page, base_url: Rails.application.config.x.public_url)
    @page = page
    @base_url = base_url
  end

  def canonical_url
    Content.canonical_url(page.url_path, host: base_url)
  end

  def last_modified = page.updated_on.iso8601
end
