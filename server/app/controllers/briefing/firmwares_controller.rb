class Briefing::FirmwaresController < Briefing::ApplicationController
  def show
    releases = Release.published
    @images = Release.firmware_shelf(Release.shelf(releases))
    @changelog = Release.changelog(releases, I18n.locale)
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    @unavailable = true
  end
end
