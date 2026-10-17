module Analytics
  class Visit
    class Computation
      def initialize(ahoy_visit)
        @ahoy_visit = ahoy_visit
        @events = ahoy_visit.events.order(:time)
      end

      def save!
        return unless browser_events?

        analytics_visit.update!(analytics_visit_attributes)
      end

      private
        def analytics_visit
          @analytics_visit ||= Visit.find_or_initialize_by(ahoy_visit_id: @ahoy_visit.id)
        end

        def analytics_visit_attributes
          {
            pageviews_count: page_paths.count,
            visit_duration: duration.in_seconds,
            entry_path: page_paths.entry,
            exit_path: page_paths.exit,
            page_paths: page_paths.all,
            last_event_at: last_event_time,
            segment: segment,
            channel: channel.name,
            entry_section: section.name,
            user_id: resolved_user_id
          }
        end

        def last_event_time
          @events.last&.time
        end

        def page_paths
          @page_paths ||= PagePaths.new(@events)
        end

        def duration
          Duration.new(@events)
        end

        def channel
          Channel.new(@ahoy_visit)
        end

        def section
          Section.new(@events)
        end

        def segment
          if User.admin.exists?(id: browser_user_ids)
            :internal
          elsif resolved_user_id
            :user
          else
            :non_user
          end
        end

        def resolved_user_id
          @ahoy_visit.user_id || browser_user_ids.first
        end

        def browser_events?
          @events.any? { it.name.start_with?("$") }
        end

        def browser_user_ids
          @browser_user_ids ||= begin
            visits = Ahoy::Visit.where(visitor_token: @ahoy_visit.visitor_token)
            events = Ahoy::Event.where(visit_id: visits.select(:id))

            (visits.where.not(user_id: nil).order(:started_at).pluck(:user_id) + events.where.not(user_id: nil).pluck(:user_id)).uniq
          end
        end
    end
  end
end
