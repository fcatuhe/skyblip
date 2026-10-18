module Analytics
  class Visitors
    DEFAULT_PERIOD = 24.hours
    PER_PAGE = 50

    Summary = Data.define(
      :visitor_token, :visits_count, :views_count, :visit_duration, :latest_views_count, :latest_visit_duration,
      :browser, :os, :last_seen, :user_id, :last_entry_path
    )

    Page = Data.define(:number, :summaries, :more) do
      def more? = more
    end

    attr_reader :filter_params

    def initialize(filter_params: {})
      @filter_params = Analytics.with_default_period(filter_params, period: DEFAULT_PERIOD)
    end

    def page(number)
      number = [ number.to_i, 1 ].max
      rows = relation.offset((number - 1) * PER_PAGE).limit(PER_PAGE + 1).to_a

      Page.new(number:, summaries: summaries(rows.first(PER_PAGE)), more: rows.size > PER_PAGE)
    end

    private
      def summaries(rows)
        visitor_tokens = rows.map { it[:visitor_token] }

        if visitor_tokens.any?
          visits_by_visitor_token = visits_grouped_by_visitor_token(visitor_tokens)
          visitor_tokens.filter_map do |visitor_token|
            to_summary(visitor_token:, visits: visits_by_visitor_token.fetch(visitor_token, []))
          end
        else
          []
        end
      end

      def relation
        @relation ||= begin
          ahoy_visits = Ahoy::Visit.arel_table
          analytics_visits = Analytics::Visit.arel_table
          last_seen = Arel::Nodes::NamedFunction.new("COALESCE", [ analytics_visits[:last_event_at], ahoy_visits[:started_at] ])
          latest_last_seen = Arel::Nodes::NamedFunction.new("MAX", [ last_seen ])

          scope
            .select(ahoy_visits[:visitor_token], latest_last_seen.as("last_seen"))
            .group(ahoy_visits[:visitor_token])
            .order(latest_last_seen.desc)
        end
      end

      def scope
        Analytics::Visit.filtered(filter_params)
      end

      def visits_grouped_by_visitor_token(visitor_tokens)
        scope
          .where(ahoy_visits: { visitor_token: visitor_tokens })
          .includes(:ahoy_visit)
          .order(Arel.sql("ahoy_visits.visitor_token, ahoy_visits.started_at, analytics_visits.id"))
          .to_a
          .group_by(&:visitor_token)
      end

      def to_summary(visitor_token:, visits:)
        if visits.any?
          latest_visit = latest_visit_for(visits)

          Summary.new(
            visitor_token: visitor_token,
            visits_count: visits.count,
            views_count: visits.sum { it.pageviews_count.to_i },
            visit_duration: visits.sum { it.visit_duration.to_i },
            latest_views_count: latest_visit.pageviews_count.to_i,
            latest_visit_duration: latest_visit.visit_duration.to_i,
            browser: latest_visit.browser,
            os: latest_visit.os,
            last_seen: last_seen_for_visits(visits),
            user_id: user_id_for(visits),
            last_entry_path: latest_visit.entry_path
          )
        end
      end

      def last_seen_for(visit)
        visit.last_event_at || visit.started_at
      end

      def latest_visit_for(visits)
        visits.max_by { [ last_seen_for(it), it.id ] }
      end

      def last_seen_for_visits(visits)
        visits.map { last_seen_for(it) }.compact.max
      end

      def user_id_for(visits)
        visits.find { it.user_id }&.user_id
      end
  end
end
