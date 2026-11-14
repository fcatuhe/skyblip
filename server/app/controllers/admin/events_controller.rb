class Admin::EventsController < Admin::ApplicationController
  FILTERS = %i[name device browser fingerprint request_id].freeze
  SHOWN = 500

  def index
    @filters = params.permit(*FILTERS).to_h.symbolize_keys.compact_blank
    @filters[:browser] = @filters[:browser].delete("-").upcase if @filters[:browser]
    @events = Event.tail(**@filters, last: SHOWN)
    @counts = Event.since(30.days.ago).group(:name).count
    @last_seen = Event.group(:name).maximum(:occurred_at)
  end
end
