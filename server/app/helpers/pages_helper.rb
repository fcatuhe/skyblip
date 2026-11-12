module PagesHelper
  def link_to_page(slug, label: nil, label_type: :title, anchor: nil, **html_options)
    page = find_page(slug)
    label ||= label_type == :nav ? page.nav_label : page.title

    lang = page.locale unless page.locale == I18n.locale
    link_to label, page_url_for(page, only_path: true, anchor:), { lang: }.merge(html_options)
  end

  def legal_notice(field) = Rails.application.credentials.dig(:legal, field)

  def on_page?(slug)
    @page&.base_slug == slug
  end

  def pages_image_tag(path, options = {})
    content_image_tag "pages/#{@page.base_slug}/#{path}", options
  end

  def screen_image_tag(path, options = {})
    content_image_tag "pages/skyblip-go/#{path}", options
  end

  def content_image_tag(path, options = {})
    width, height = Content::Image.dimensions(path)
    image_tag path, { width:, height: }.merge(options)
  end

  def seeed_promo_code
    code = Rails.configuration.x.seeed.promo_code

    tag.button type: "button", class: "promo-code", data: {
      controller: "copy-to-clipboard", action: "copy-to-clipboard#copy",
      copy_to_clipboard_content_value: code, copy_to_clipboard_success_class: "promo-code--copied"
    } do
      safe_join [
        tag.code(code),
        tag.span(t("promo_code.copy"), class: "promo-code-copy"),
        tag.span(t("promo_code.copied"), class: "promo-code-copied", aria: { live: "polite" })
      ]
    end
  end

  private
    def find_page(slug)
      page = Content::Page.find_by(base_slug: slug) || Content::Page.find_by(base_slug: slug, locale: I18n.default_locale)
      raise Decant::FileNotFound, "Couldn't find Content::Page with 'base_slug'=#{slug} and 'locale'=#{I18n.locale}" unless page

      page
    end
end
