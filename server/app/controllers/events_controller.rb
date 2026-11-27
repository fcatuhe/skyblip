class EventsController < ApplicationController
  ENTRY = [ :id, :name, :at, :seq, :page, :path, :device, properties: {} ].freeze
  # INFO: fc 20nov26 a keepalive fetch carries 64 KB at most, so a larger post comes from no browser
  LARGEST = 64.kilobytes

  allow_unauthenticated_access
  skip_around_action :switch_locale
  skip_after_action :track_action
  rate_limit to: 60, within: 1.minute, with: -> { head :too_many_requests }
  before_action -> { head :content_too_large }, if: -> { request.content_length > LARGEST }

  def create
    journal = Event::Journal.new(browser: params[:browser], entries: params.expect(entries: [ ENTRY ]), sent_at: params[:sent_at],
      user: Current.user, request_id: request.request_id, ip_address: request.remote_ip)

    if journal.valid?
      journal.record unless ahoy.exclude?
      head :no_content
    else
      head :unprocessable_content
    end
  end
end
