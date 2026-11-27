class Briefing::PreferencesController < Briefing::ApplicationController
  def show
    @preferences = preferences
  end

  def update
    @preferences = preferences
    @preferences.assign_attributes(preferences_params)

    if @preferences.valid?
      cookies.permanent[:locale] = { value: @preferences.locale, same_site: :lax }
      cookies.permanent[:development_builds] = { value: @preferences.development_builds, same_site: :lax }
      redirect_to briefing_preferences_path
    else
      render :show, status: :unprocessable_entity
    end
  end

  private
    def preferences_params
      params.expect(preferences: [ :locale, :development_builds ])
    end
end
