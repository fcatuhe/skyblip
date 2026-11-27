class Briefing::ApplicationController < ApplicationController
  allow_unauthenticated_access

  layout -> { turbo_frame_request? ? "turbo_rails/frame" : "briefing" }

  private
    def preferences = Briefing::Preferences.new(locale: I18n.locale, development_builds: cookies.fetch(:development_builds, false))

    def requested_locale = embedding_locale || chosen_locale || browser_locale || I18n.default_locale

    def embedding_locale = params[:locale].presence_in(Briefing::Preferences.locales)

    def chosen_locale = cookies[:locale].presence_in(Briefing::Preferences.locales)

    def browser_locale = browser_locales.find { it.in?(Briefing::Preferences.locales) }

    def default_url_options = {}
end
