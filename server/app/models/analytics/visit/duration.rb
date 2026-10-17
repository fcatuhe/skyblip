module Analytics
  class Visit
    class Duration
      VISIBILITY_EVENTS = %w[$visibilitychange $pagehide].freeze

      def initialize(events)
        @events = events.select(&:time)
      end

      def in_seconds
        if @events.size < 2
          0
        elsif visibility_tracked?
          visible_seconds
        else
          (@events.last.time - @events.first.time).to_i
        end
      end

      private

        def visibility_tracked?
          @events.any? { it.name.in?(VISIBILITY_EVENTS) }
        end

        def visible_seconds
          visible_since = @events.first.time
          accumulated = 0

          @events.each do |event|
            case event.name
            when "$visibilitychange"
              visible_since, accumulated = close_visible_span(visible_since, accumulated, at: event.time) if event.props[:state] == "hidden"
              visible_since = open_visible_span(at: event.time) if event.props[:state] == "visible"
            when "$pagehide"
              visible_since, accumulated = close_visible_span(visible_since, accumulated, at: event.time)
            else
              visible_since ||= open_visible_span(at: event.time)
            end
          end

          visible_since, accumulated = close_visible_span(visible_since, accumulated, at: @events.last.time)
          accumulated.to_i
        end

        def open_visible_span(at:)
          at
        end

        def close_visible_span(visible_since, accumulated, at:)
          if visible_since
            [ nil, accumulated + (at - visible_since) ]
          else
            [ visible_since, accumulated ]
          end
        end
    end
  end
end
