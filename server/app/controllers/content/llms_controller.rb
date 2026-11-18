class Content::LlmsController < ApplicationController
  allow_unauthenticated_access

  def show
    @pages = Content::Page.all.select { it.locale == I18n.default_locale }.index_by(&:base_slug)
  end
end
