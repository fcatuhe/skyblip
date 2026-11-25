class Content::LlmsFullController < ApplicationController
  allow_unauthenticated_access

  def show
    @pages = Content::Page.read_in(I18n.locale).reject { it.base_slug == "legal-notice" }
  end
end
