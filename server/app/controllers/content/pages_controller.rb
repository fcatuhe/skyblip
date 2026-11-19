class Content::PagesController < ApplicationController
  allow_unauthenticated_access

  before_action :negotiate_format
  rescue_from Decant::FileNotFound, with: :page_not_found

  def show
    @page = Content::Page.find(params[:slug])

    if @format == :md
      response.headers["Link"] = %(<#{Content.canonical_url(@page.url_path)}>; rel="canonical", <#{@page.url_path}>; rel="alternate"; type="text/html")
      render markdown: @page
    else
      response.headers["Link"] = %(<#{@page.markdown_path}>; rel="alternate"; type="text/markdown")
      render formats: :html
    end
  end

  private
    def negotiate_format
      response.headers["Vary"] = "Accept"
      @format = params[:format] == "md" ? :md : Content::Negotiation.new(request.accept).format

      if @format
        Rails.event.set_context(format: @format)
      else
        head :not_acceptable
      end
    end

    def page_not_found(error)
      if @format == :md
        render :missing, formats: :md, status: :not_found
      else
        raise error
      end
    end
end
