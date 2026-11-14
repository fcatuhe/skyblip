module Tracing
  extend ActiveSupport::Concern

  FRAME = /\A[a-z0-9_-]{1,100}\z/i
  BROWSER_HEADER = "X-Briefing-Browser"

  included do
    before_action :trace_request
  end

  private
    def trace_request
      Rails.event.set_context({
        request_id: request.request_id, method: request.method, path: request.path, format: request.format.to_sym,
        frame: request.headers["Turbo-Frame"].to_s[FRAME], prefetch: request.headers["X-Sec-Purpose"] == "prefetch",
        browser: request.headers[BROWSER_HEADER].to_s[Event::Journal::BROWSER],
        device: (params[:device_slug] || params[:slug]).to_s[Event::Journal::DEVICE]
      }.compact_blank)
    end
end
