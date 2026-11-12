class Briefing::FirmwaresController < Briefing::ApplicationController
  def show
    @images = Release.firmware_shelf(Release.all)
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    @unavailable = true
  end
end
