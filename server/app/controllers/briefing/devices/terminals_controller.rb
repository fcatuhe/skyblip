class Briefing::Devices::TerminalsController < Briefing::ApplicationController
  def show
    @slug = params[:device_slug]
  end
end
