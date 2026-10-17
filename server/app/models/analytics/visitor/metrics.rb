module Analytics
  class Visitor
    module Metrics
      def metrics
        @metrics ||= begin
          row = metrics_row

          {
            visits: row.visits_count,
            views: row.views,
            events: server_events_count,
            visit_duration: row.visit_duration || 0
          }
        end
      end

      private
        def metrics_row
          @metrics_row ||= analytics_visits
            .select(*metrics_selects)
            .take
        end

        def metrics_selects
          [
            "COUNT(*) AS visits_count",
            "SUM(analytics_visits.pageviews_count) AS views",
            "SUM(analytics_visits.visit_duration) AS visit_duration"
          ]
        end

        def server_events_count
          Ahoy::Event
            .where(visit_id: analytics_visits.select(:ahoy_visit_id))
            .server_side
            .count
        end
    end
  end
end
