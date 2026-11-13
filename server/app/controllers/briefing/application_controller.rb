class Briefing::ApplicationController < ApplicationController
  allow_unauthenticated_access

  layout -> { turbo_frame_request? ? "turbo_rails/frame" : "briefing" }

  private
    def requested_locale = embedding_locale || chosen_locale || browser_locale || I18n.default_locale

    def embedding_locale = params[:locale].presence_in(Briefing::Preferences.locales)

    def chosen_locale = cookies[:locale].presence_in(Briefing::Preferences.locales)

    def browser_locale
      languages = request.headers["Accept-Language"].to_s.split(",").map { it.split(";").first.strip.split("-").first.downcase }
      languages.find { it.in?(Briefing::Preferences.locales) }
    end

    def default_url_options = {}
end
