Rails.application.config.to_prepare do
  # INFO: fc 14nov26 the gem's English-only I18n config drops French when its request loads the translations, and admin already speaks English
  MissionControl::Jobs::ApplicationController.skip_around_action :set_current_locale
end
