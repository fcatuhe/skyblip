module Analytics
  class Overview
    include Breakdowns

    BREAKDOWN_LIMIT = 10
    SERIES = %i[pageviews visits visitors].freeze
    HOURLY_MAX_DAYS = 2

    attr_reader :filter_params

    def initialize(filter_params: {})
      @filter_params = Analytics.with_default_period(filter_params)
    end

    def metrics = scope.metrics

    def series
      rows = series_rows

      SERIES.index_with { empty_buckets.merge(series_values(rows: rows, key: it)) }
    end

    def chart = Chart.new(series, hourly: hourly?)

    def hourly? = period_in_days <= HOURLY_MAX_DAYS

    private
      def scope
        Analytics::Visit.filtered(filter_params)
      end

      def series_rows
        @series_rows ||= scope
          .group(time_grain)
          .order(Arel.sql(time_grain))
          .select(*series_selects)
      end

      def series_selects
        [
          "#{time_grain} AS period",
          "COUNT(*) AS visits",
          "COUNT(DISTINCT ahoy_visits.visitor_token) AS visitors",
          "SUM(analytics_visits.pageviews_count) AS pageviews"
        ]
      end

      def series_values(rows:, key:)
        rows.each_with_object({}) do |row, values|
          values[row.period] = row.public_send(key)
        end
      end

      def empty_buckets = @empty_buckets ||= bucket_periods.index_with(0)

      def bucket_periods
        step = hourly? ? 1.hour : 1.day
        last = bucket_start_of(filter_params[:end_on].to_date.end_of_day.utc)

        (0..).lazy
             .map { bucket_start_of(filter_params[:start_on].to_date.beginning_of_day.utc) + (it * step) }
             .take_while { it <= last }
             .map { it.strftime(bucket_format) }
             .select { requested_dates.include?(it.first(10)) }
             .to_a
      end

      def requested_dates
        @requested_dates ||= (filter_params[:start_on].to_date..filter_params[:end_on].to_date)
                             .map { it.strftime("%Y-%m-%d") }.to_set
      end

      def bucket_start_of(time) = hourly? ? time.beginning_of_hour : time.beginning_of_day

      def bucket_format = hourly? ? "%Y-%m-%d %H:00:00" : "%Y-%m-%d"

      def time_grain
        @time_grain ||= if hourly?
          hourly_time_grain
        else
          daily_time_grain
        end
      end

      def period_in_days
        (filter_params[:end_on].to_date - filter_params[:start_on].to_date).to_i
      end

      def hourly_time_grain
        "strftime('%Y-%m-%d %H:00:00', ahoy_visits.started_at)"
      end

      def daily_time_grain
        "strftime('%Y-%m-%d', ahoy_visits.started_at)"
      end
  end
end
