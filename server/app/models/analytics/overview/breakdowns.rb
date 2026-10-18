module Analytics
  class Overview
    module Breakdowns
      def breakdowns
        {
          path: path_breakdown,
          entry: breakdown(column: "analytics_visits.entry_path"),
          exit: breakdown(column: "analytics_visits.exit_path"),
          referrer: breakdown(column: referrer_label_column),
          section: breakdown(column: "analytics_visits.entry_section"),
          channel: breakdown(column: "analytics_visits.channel"),
          browser: breakdown(column: "ahoy_visits.browser"),
          os: breakdown(column: "ahoy_visits.os"),
          device: breakdown(column: "ahoy_visits.device_type"),
          country: breakdown(column: "ahoy_visits.country", include_country_code: true),
          region: breakdown(column: "ahoy_visits.region", include_country_code: true),
          city: breakdown(column: "ahoy_visits.city", include_country_code: true)
        }
      end

      private
        def path_breakdown
          rows = path_breakdown_rows
          duration_by_path = average_duration_by_path(rows.map(&:label))

          rows.map do |row|
            breakdown_entry_for(row, duration: duration_by_path[row.label] || 0)
          end
        end

        def path_breakdown_rows
          scope
            .joins("CROSS JOIN json_each(analytics_visits.page_paths) AS page_paths")
            .group(Arel.sql("page_paths.value"))
            .select(
              Arel.sql("page_paths.value AS label"),
              "COUNT(DISTINCT ahoy_visits.visitor_token) AS visitors",
              "COUNT(*) AS pageviews"
            )
            .order(Arel.sql("pageviews DESC, visitors DESC, label ASC"))
            .limit(BREAKDOWN_LIMIT)
        end

        def breakdown(column:, include_country_code: false)
          extra_selects = include_country_code ? [ "ahoy_visits.country_code" ] : []

          scope
            .group(Arel.sql(column))
            .select(
              Arel.sql("#{column} AS label"),
              *breakdown_selects,
              *extra_selects
            )
            .order(Arel.sql("pageviews DESC, visitors DESC, label ASC"))
            .limit(BREAKDOWN_LIMIT)
            .map { |row| breakdown_entry_for(row, include_country_code: include_country_code) }
        end

        def breakdown_selects
          [
            "COUNT(DISTINCT ahoy_visits.visitor_token) AS visitors",
            "SUM(analytics_visits.pageviews_count) AS pageviews",
            "CAST(ROUND(AVG(analytics_visits.visit_duration)) AS INTEGER) AS duration"
          ]
        end

        def average_duration_by_path(labels)
          if labels.present?
            distinct_paths = distinct_paths_for_duration(labels)
            average_duration_rows_for(distinct_paths)
          else
            {}
          end
        end

        def distinct_paths_for_duration(labels)
          scope
            .joins("CROSS JOIN json_each(analytics_visits.page_paths) AS page_paths")
            .where("page_paths.value IN (?)", labels)
            .select(
              "DISTINCT analytics_visits.ahoy_visit_id AS ahoy_visit_id",
              "page_paths.value AS label",
              "analytics_visits.visit_duration AS visit_duration"
            )
        end

        def average_duration_rows_for(distinct_paths)
          Ahoy::Visit
            .unscoped
            .from("(#{distinct_paths.to_sql}) AS distinct_paths")
            .group("distinct_paths.label")
            .select(
              "distinct_paths.label AS label",
              "CAST(ROUND(AVG(distinct_paths.visit_duration)) AS INTEGER) AS duration"
            )
            .each_with_object({}) do |row, memo|
              memo[row.label] = row.duration
            end
        end

        def referrer_label_column
          "COALESCE(NULLIF(ahoy_visits.referring_domain, ''), 'Direct')"
        end

        def breakdown_entry_for(row, duration: row.duration, include_country_code: false)
          entry = {
            label: row.label.presence || "Unknown",
            filter_value: row.label.presence || "unknown",
            visitors: row.visitors,
            pageviews: row.pageviews,
            duration: duration
          }

          if include_country_code && row.label.present?
            entry[:country_code] = row[:country_code]
          end

          entry
        end
    end
  end
end
