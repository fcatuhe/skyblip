Content::Page = Decant.define(dir: Rails.root.join("content/pages"), ext: "html.erb") do
  include Localizable

  frontmatter :title
  frontmatter :description
  frontmatter :nav_title

  def self.find(slug, locale: I18n.locale)
    locale = locale.to_sym

    all.find { |page| page.locale == locale && page.to_param == slug } ||
      raise(Decant::FileNotFound, "Couldn't find #{name} with 'slug'=#{slug} and 'locale'=#{locale}")
  end

  def self.routable?(slug, locale: I18n.default_locale)
    all.any? { |page| page.locale == locale.to_sym && page.base_slug != "index" && page.to_param == slug }
  end

  def url_path
    segments = [ locale_parameter, (to_param unless base_slug == "index") ].compact
    "/#{segments.join("/")}"
  end

  def to_param
    frontmatter&.dig(:slug)&.to_s.presence || base_slug
  end

  def nav_label
    nav_title.presence || title
  end
end
