module Localized
  extend ActiveSupport::Concern

  included do
    around_action :switch_locale
    helper_method :browser_locales
  end

  private
    def switch_locale(&action)
      I18n.with_locale(requested_locale, &action)
    end

    def requested_locale = params[:locale].presence_in(I18n.available_locales.map(&:to_s)) || I18n.default_locale

    def browser_locales
      ranges = request.accept_language.to_s.split(",").map { it.strip.split(";q=") }
      ranges = ranges.reject { |tag, quality| tag.blank? || quality&.to_f&.zero? }
      ranges.sort_by.with_index { |(_, quality), index| [ -(quality || 1).to_f, index ] }.map { |tag, _| tag.downcase.split("-").first }.uniq
    end

    def default_url_options
      return {} if I18n.locale == I18n.default_locale

      { locale: I18n.locale }
    end
end
