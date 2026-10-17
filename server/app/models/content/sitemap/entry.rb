class Content::Sitemap::Entry
  attr_reader :page, :base_url

  def initialize(page, base_url: Rails.application.config.x.public_url)
    @page = page
    @base_url = base_url
  end

  def canonical_url
    Content.canonical_url(page.url_path, host: base_url)
  end

  def change_frequency
    "monthly"
  end

  def priority
    case page.url_path
    when "/" then "1.0"
    when /^\/[^\/]+$/ then "0.8"
    else "0.5"
    end
  end
end
