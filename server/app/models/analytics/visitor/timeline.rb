module Analytics
  class Visitor
    class Timeline
      include SourceFiltering
      include SessionMarkers

      VISITS_PER_PAGE = 10

      attr_reader :page

      def initialize(visitor_token:, source: "browser", page: 1, per_page: VISITS_PER_PAGE)
        @visitor_token = visitor_token
        @source = source
        @page = page.to_i
        @per_page = per_page
      end

      def groups
        @groups ||= build_groups
      end

      def more?
        paginated_visits.size > @per_page
      end

      private
        attr_reader :source

        def build_groups
          entries = source_events.map { Entry.from_event(it) } + session_markers
          sorted_entries = entries.sort_by { it.time || Time.zone.at(0) }.reverse

          sorted_entries
            .group_by { it.time&.to_date }
            .sort_by { |date, _| date }
            .reverse
            .map { |date, day_entries| { date: date, events: day_entries } }
        end

        def paginated_visits
          @paginated_visits ||= Analytics::Visit
            .joins(:ahoy_visit)
            .includes(ahoy_visit: :events)
            .where(ahoy_visits: { visitor_token: @visitor_token })
            .order(Arel.sql("ahoy_visits.started_at DESC"))
            .offset((@page - 1) * @per_page)
            .limit(@per_page + 1)
            .to_a
        end

        def visits
          @visits ||= paginated_visits.first(@per_page)
        end
    end
  end
end
