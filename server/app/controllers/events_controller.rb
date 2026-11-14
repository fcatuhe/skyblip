class EventsController < ApplicationController
  ENTRY = [ :id, :name, :at, :seq, :page, :path, :device, properties: {} ].freeze

  allow_unauthenticated_access
  skip_around_action :switch_locale
  skip_after_action :track_action
  rate_limit to: 60, within: 1.minute, by: :network_pseudonym, with: -> { head :too_many_requests }

  def create
    journal = Event::Journal.new(browser: params[:browser], entries: params.expect(entries: [ ENTRY ]),
      user: Current.user, request_id: request.request_id)

    if journal.valid?
      journal.record
      head :no_content
    else
      head :unprocessable_content
    end
  end
end
