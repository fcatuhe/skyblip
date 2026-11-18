class Briefing::InstallersController < Briefing::ApplicationController
  def show
    @installers = Release.installers
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    @unavailable = true
  end
end
