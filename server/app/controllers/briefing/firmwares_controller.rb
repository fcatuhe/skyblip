class Briefing::FirmwaresController < ApplicationController
  def show
    @releases = Release.all
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    @unavailable = true
  end
end
