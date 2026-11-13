module LocaleHelper
  def crawler? = DeviceDetector.new(request.user_agent).bot?

  def page_alternate_paths
    @page_alternate_paths ||= if @page
      page_alternates.transform_values { |page| page_url_for(page, only_path: true) }
    else
      route_alternates.transform_values { |options| url_for(options.merge(request.query_parameters.symbolize_keys.except(*options.keys))) }
    end
  end

  def hreflang_link_tags
    urls = if @page
      page_alternates.transform_values { |page| page_url_for(page) }
    else
      route_alternates.transform_values { |options| Content.canonical_url(url_for(options)) }
    end
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

    def route_alternates
      if localized_route?
        I18n.available_locales.index_with do |locale|
          request.path_parameters.merge(locale: (locale unless locale == I18n.default_locale), only_path: true)
        end
      else
        {}
      end
    end

    def localized_route?
      request.route_uri_pattern.to_s.start_with?("(/:locale)/")
    end
end
