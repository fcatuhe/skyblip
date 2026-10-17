module Analytics
  class Visit
    class PagePaths
      def initialize(events)
        @events = events
      end

      def all
        @all ||= pageview_events.filter_map { it.props[:page] }
      end

      def entry
        all.first
      end

      def exit
        all.last
      end

      def count
        pageview_events.size
      end

      private

        def pageview_events
          @pageview_events ||= @events.select { it.name == "$view" }
        end
    end
  end
end
