class Content::LlmsController < ApplicationController
  allow_unauthenticated_access

  def show
    @pages = Content::Page.read_in(I18n.locale).index_by(&:base_slug)
  end
end
