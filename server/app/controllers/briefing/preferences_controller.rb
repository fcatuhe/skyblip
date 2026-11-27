class Briefing::PreferencesController < Briefing::ApplicationController
  def show
    @preferences = preferences
  end

  def update
    @preferences = Briefing::Preferences.new(preferences_params)
    cookies.permanent[:development_builds] = { value: @preferences.development_builds, same_site: :lax }
    redirect_to briefing_preferences_path
  end

  private
    def preferences_params
      params.expect(preferences: [ :development_builds ])
    end
end
