class Briefing::Devices::FirmwaresController < Briefing::ApplicationController
  def show
    @slug = params[:device_slug]
  end
end
