class Briefing::PreferencesController < Briefing::ApplicationController
  def show
    @preferences = Briefing::Preferences.new(locale: I18n.locale)
  end

  def update
    @preferences = Briefing::Preferences.new(preferences_params)

    if @preferences.valid?
      cookies.permanent[:locale] = { value: @preferences.locale, same_site: :lax }
      redirect_to briefing_preferences_path
    else
      render :show, status: :unprocessable_entity
    end
  end

  private
    def preferences_params
      params.expect(preferences: [ :locale ])
    end
end
