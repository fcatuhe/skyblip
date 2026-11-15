class Admin::ErrorsController < Admin::ApplicationController
  def index
    @days = params[:days].to_i.presence_in(Event::Review::PERIODS) || Event::Review::PERIODS.first
    @since = @days.days.ago
    @resolved, @errors = Event::Error.since(@since).partition(&:resolved?)
  end

  def show
    @error = Event::Error.find_by(fingerprint: params[:fingerprint]) || raise(ActiveRecord::RecordNotFound)
  end
end
