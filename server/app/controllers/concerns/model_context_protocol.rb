module ModelContextProtocol
  extend ActiveSupport::Concern

  included do
    include Tracing
  end

  def show = serve

  def create = serve

  def destroy = serve

  private
    def serve
      status, headers, body = transport.handle_request(request)

      self.status = status
      response.headers.merge! headers
      self.response_body = body
    end

    def allowed_hosts = [ Rails.application.routes.default_url_options.fetch(:host) ]

    def icons
      [ { src: Content.canonical_url("/icon.svg"), mime_type: "image/svg+xml", sizes: [ "any" ] },
        { src: Content.canonical_url("/icon.png"), mime_type: "image/png", sizes: [ "512x512" ] } ]
    end
end
