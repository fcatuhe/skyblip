Content::Page = Decant.define(dir: Rails.root.join("content/pages"), ext: "html.erb") do
  include Localizable

  frontmatter :title
  frontmatter :description
  frontmatter :nav_title

  # INFO: fc 19nov26 every link in the nav and the footer looks a page up, so the pages are read once a process and not once a link
  def self.all = @all ||= super

  def self.find(slug, locale: I18n.locale)
    locale = locale.to_sym

    all.find { |page| page.locale == locale && page.to_param == slug } ||
      raise(Decant::FileNotFound, "Couldn't find #{name} with 'slug'=#{slug} and 'locale'=#{locale}")
  end

  def self.routable?(slug, locale: I18n.default_locale)
    all.any? { |page| page.locale == locale.to_sym && page.base_slug != "index" && page.to_param == slug }
  end

  def self.at(path)
    path = path.to_s.delete_suffix("/").presence || "/"

    all.find { |page| page.url_path == path || page.markdown_path == path }
  end

  def self.read_in(locale)
    locale = locale.to_sym
    translated = all.select { it.locale == locale }
    slugs = translated.map(&:base_slug)
    untranslated = all.select { it.locale == I18n.default_locale && slugs.exclude?(it.base_slug) }

    (translated + untranslated).sort_by { it.base_slug == "index" ? "" : it.to_param }
  end

  def url_path
    segments = [ locale_parameter, (to_param unless base_slug == "index") ].compact
    "/#{segments.join("/")}"
  end

  def markdown_path
    base_slug == "index" ? "#{url_path.delete_suffix("/")}/index.md" : "#{url_path}.md"
  end

  def canonical_url = Content.canonical_url(url_path)

  def markdown_url = Content.canonical_url(markdown_path)

  # INFO: fc 25nov26 the pages change only with a deploy, and Content::Page.all keeps each one for the life of the process
  def markdown = @markdown ||= to_markdown

  def to_markdown
    html = I18n.with_locale(locale) { Content::PagesController.render(:show, assigns: { page: self }, layout: false, formats: :html) }

    Content::Markdown.new(html).to_markdown
  end

  def slug = @slug ||= super

  def to_param
    frontmatter&.dig(:slug)&.to_s.presence || base_slug
  end

  def nav_label
    nav_title.presence || title
  end

  def updated_on
    Date.iso8601(frontmatter.fetch(:updated_on))
  end

  def published_on
    frontmatter[:published_on]&.then { Date.iso8601(it) }
  end

  def article? = published_on.present?

  def parent
    frontmatter[:parent]&.then { self.class.find_by(base_slug: it, locale:) }
  end

  def reading_order
    @reading_order ||= sort_for_reading(self.class.all.select { it.locale == locale && it.frontmatter[:parent] == frontmatter[:parent] })
  end

  def reading_position = reading_order.index(self) + 1

  def previous_page
    if reading_position > 1
      reading_order[reading_position - 2]
    end
  end

  def next_page = reading_order[reading_position]

  def share_image = frontmatter[:share_image]

  def product = frontmatter[:product]

  private
    def sort_for_reading(siblings)
      if order = listed_order
        siblings.sort_by { order.index(it.base_slug) || raise(KeyError, "#{it.base_slug} is missing from the reading order of #{frontmatter[:parent]}") }
      else
        siblings.sort_by { [ it.published_on, it.base_slug ] }
      end
    end

    def listed_order
      case frontmatter[:parent]
      when "guides" then %w[mount go-first-steps bravo-first-steps radar instruments displays controls power phone radio timing gps traffic]
      when "ads-l" then %w[ads-l/registry ads-l/callsign ads-l/conformity ads-l/nmea ads-l/questions]
      end
    end
end
