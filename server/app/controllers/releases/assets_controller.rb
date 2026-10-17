class Releases::AssetsController < ApplicationController
  # INFO: fc 17oct26 Rails refuses a GET answered with JavaScript unless forgery protection is off, and embed.js is one.
  skip_forgery_protection
  allow_unauthenticated_access

  def show
    bytes = Release.download(params[:release_tag], params[:file])
    expires_in 1.year, public: true, immutable: true
    send_data bytes, type: content_type, disposition: "inline"
  rescue Release::NotFound
    head :not_found
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    head :bad_gateway
  end

  private
    def content_type
      case File.extname(params[:file])
      when ".wasm" then "application/wasm"
      when ".js" then "text/javascript"
      when ".json" then "application/json"
      else "application/octet-stream"
      end
    end
end
