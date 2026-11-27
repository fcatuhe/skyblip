class Briefing::InstallersController < Briefing::ApplicationController
  def show
    @installers = Release.installers(development: preferences.development_builds)
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    @unavailable = true
  end
end
