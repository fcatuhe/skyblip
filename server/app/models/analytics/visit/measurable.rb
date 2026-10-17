module Analytics
  class Visit
    module Measurable
      extend ActiveSupport::Concern

      NO_METRICS = { visitors: 0, visits: 0, pageviews: 0, bounce_rate: 0, visit_duration: 0 }.freeze

      class_methods do
        def metrics
          row = joins(:ahoy_visit).select(*metrics_selects).take

          if row
            {
              visitors: row.visitors,
              visits: row.visits,
              pageviews: row.pageviews || 0,
              bounce_rate: bounce_rate_of(row),
              visit_duration: row.visit_duration || 0
            }
          else
            NO_METRICS
          end
        end

        private
          def metrics_selects
            [
              "COUNT(DISTINCT ahoy_visits.visitor_token) AS visitors",
              "COUNT(*) AS visits",
              "SUM(analytics_visits.pageviews_count) AS pageviews",
              "SUM(CASE WHEN analytics_visits.pageviews_count = 1 THEN 1 ELSE 0 END) AS bounced",
              "CAST(ROUND(AVG(analytics_visits.visit_duration)) AS INTEGER) AS visit_duration"
            ]
          end

          def bounce_rate_of(row)
            if row.visits.positive?
              row.bounced.to_f / row.visits
            else
              0
            end
          end
      end
    end
  end
end
