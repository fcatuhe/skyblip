class Admin::ErrorsController < Admin::ApplicationController
  def index
    @days = params[:days].to_i.presence_in(Event::Review::PERIODS) || Event::Review::PERIODS.first
    @since = @days.days.ago
    @problems = Event::Problem.since(@since)
  end

  def show
    @problem = Event::Problem.find(params[:fingerprint]) || raise(ActiveRecord::RecordNotFound)
  end
end
