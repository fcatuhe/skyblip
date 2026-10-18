class Briefing::FirmwaresController < ApplicationController
  allow_unauthenticated_access

  def show
    @releases = Release.all
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    @unavailable = true
  end
end
