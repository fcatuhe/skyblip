module Analytics
  class Visit
    class Section
      CONTROLLER_PATTERNS = {
        admin: /\A(admin\/|mission_control\/|sessions\z)/,
        briefing: /\Abriefing\//
      }.freeze

      PATH_PATTERNS = {
        admin: /\A\/(admin|session)(\/|\z)/,
        briefing: /\A(\/[a-z]{2})?\/briefing(\/|\z)/
      }.freeze

      def initialize(events)
        @events = events
      end

      def name
        @name ||= from_server_events || from_browser_events
      end

      private
        def from_server_events
          if server_events.any? { it.properties&.dig("controller").present? }
            matching(CONTROLLER_PATTERNS) { |pattern| server_events.any? { it.properties&.dig("controller")&.match?(pattern) } }
          end
        end

        def from_browser_events
          matching(PATH_PATTERNS) { |pattern| page_paths.any? { it.match?(pattern) } }
        end

        def matching(patterns)
          patterns.find { |_section, pattern| yield pattern }&.first || :public
        end

        def server_events
          @server_events ||= @events.reject { it.name.start_with?("$") }
        end

        def page_paths
          @page_paths ||= @events.select { it.name.start_with?("$") }.filter_map { it.properties&.dig("page") }
        end
    end
  end
end
