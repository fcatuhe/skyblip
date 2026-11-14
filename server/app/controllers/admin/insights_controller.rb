class Admin::InsightsController < Admin::ApplicationController
  def show
    @days = params[:days].to_i.presence_in(Event::Review::PERIODS) || Event::Review::PERIODS.first
    @review = Event::Review.new(since: @days.days.ago)
  end
end
