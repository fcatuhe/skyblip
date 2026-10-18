module Analytics
  DEFAULT_PERIOD = 30.days
  PERIOD_PRESETS = [ 1, 7, 30, 90 ].freeze

  AHOY_VISIT_FILTER_KEYS = %i[start_on end_on country region city referrer browser os device].freeze
  ANALYTICS_VISIT_FILTER_KEYS = %i[segment section channel entry exit path].freeze
  FILTER_KEYS = (AHOY_VISIT_FILTER_KEYS + ANALYTICS_VISIT_FILTER_KEYS).freeze
  DISPLAY_FILTER_KEYS = (FILTER_KEYS - %i[start_on end_on segment section]).freeze

  module_function
    def with_default_period(filter_params, period: DEFAULT_PERIOD)
      filter_params.dup.tap do |params|
        params[:start_on] = params[:start_on].presence || period.ago.to_date
        params[:end_on] = params[:end_on].presence || Date.current
      end
    end

    def parse_time(value)
      value.is_a?(String) ? Time.zone.parse(value) : value
    end
end
