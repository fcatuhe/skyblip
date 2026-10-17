class Content::PagesController < ApplicationController
  allow_unauthenticated_access

  def show
    @page = Content::Page.find(params[:slug])
  end
end
