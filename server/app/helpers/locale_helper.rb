module LocaleHelper
  def page_alternate_paths
    @page_alternate_paths ||= page_alternates.transform_values { |page| page_url_for(page, only_path: true) }
  end

  def hreflang_link_tags
    urls = page_alternates.transform_values { |page| page_url_for(page) }
    urls[:"x-default"] = urls[I18n.default_locale]

    safe_join(urls.compact.map { |locale, url| tag(:link, rel: "alternate", hreflang: locale, href: url) }, "\n")
  end

  def page_url_for(page, only_path: false, anchor: nil)
    locale = page.locale_parameter
    path = if page.base_slug == "index"
      root_path(locale:, anchor:)
    else
      page_path(page, locale:, anchor:)
    end

    only_path ? path : Content.canonical_url(path)
  end

  private
    def page_alternates
      return {} unless @page

      @page_alternates ||= I18n.available_locales.index_with { |locale| Content::Page.find_by(base_slug: @page.base_slug, locale:) }.compact
    end
end
