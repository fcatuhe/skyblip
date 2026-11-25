module Analytics
  DEFAULT_PERIOD = 30.days
  MAX_DAYS = Rails.configuration.x.retention.in_days.to_i
  PERIOD_PRESETS = [ 1, 7, 30, 90 ].freeze

  AHOY_VISIT_FILTER_KEYS = %i[start_on end_on country region city referrer browser os device].freeze
  ANALYTICS_VISIT_FILTER_KEYS = %i[segment section activation channel entry exit path outbound].freeze
  FILTER_KEYS = (AHOY_VISIT_FILTER_KEYS + ANALYTICS_VISIT_FILTER_KEYS).freeze
  DISPLAY_FILTER_KEYS = (FILTER_KEYS - %i[start_on end_on segment section activation]).freeze
  ACTIVATION_CHOICES = %w[activated].freeze

  module_function
    def with_default_period(filter_params, period: DEFAULT_PERIOD)
      filter_params.dup.tap do |params|
        start_on, end_on = [ date_from(params[:start_on]) || period.ago.to_date, date_from(params[:end_on]) || Date.current ].minmax
        params[:start_on] = [ start_on, end_on - MAX_DAYS ].max
        params[:end_on] = end_on
      end
    end

    def date_from(value)
      value.is_a?(String) ? Date.iso8601(value) : value
    rescue Date::Error
      nil
    end

    def parse_time(value)
      value.is_a?(String) ? Time.zone.parse(value) : value
    end
end
