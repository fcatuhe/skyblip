class Content::PagesController < ApplicationController
  def show
    @page = Content::Page.find(params[:slug])
  end
end
