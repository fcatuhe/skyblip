class Content::FeedsController < ApplicationController
  allow_unauthenticated_access

  def show
    @pages = Content::Page.read_in(I18n.locale).sort_by { [ it.updated_on, it.url_path ] }.reverse
  end
end
