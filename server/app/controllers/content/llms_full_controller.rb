class Content::LlmsFullController < ApplicationController
  allow_unauthenticated_access

  def show
    @pages = Content::Page.all.select { it.locale == I18n.default_locale && it.base_slug != "legal-notice" }.sort_by(&:url_path)
  end
end
