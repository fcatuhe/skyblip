module Analytics
  class Visitor
    class Timeline
      class Entry
        SERVER_DETAIL_IGNORED_KEYS = %w[controller action].freeze

        attr_reader :type, :time, :display_time, :duration

        def initialize(type:, time:, display_time: nil, duration: nil, properties: {})
          @type = type
          @time = time
          @display_time = display_time
          @duration = duration
          @properties = properties
        end

        def self.from_event(event)
          new(type: event.name, time: event.time, properties: event.props)
        end

        def self.session_start(time:, display_time:, duration:)
          new(type: :session_start, time: time, display_time: display_time, duration: duration)
        end

        def self.session_end(time:, display_time:, duration:)
          new(type: :session_end, time: time, display_time: display_time, duration: duration)
        end

        def session_start?
          type == :session_start
        end

        def session_end?
          type == :session_end
        end

        def session_marker?
          session_start? || session_end?
        end

        def view?
          type == "$view"
        end

        def click?
          type == "$click"
        end

        def submit?
          type == "$submit"
        end

        def change?
          type == "$change"
        end

        def visibility_change?
          type == "$visibilitychange"
        end

        def pagehide?
          type == "$pagehide"
        end

        def browser_event?
          type.to_s.start_with?("$")
        end

        def label
          if view?
            page_label
          elsif click?
            click_label
          elsif submit?
            submit_label
          elsif change?
            change_label
          elsif visibility_change?
            visibility_change_label
          elsif pagehide?
            "Close"
          else
            type.to_s
          end
        end

        def detail
          if click?
            click_detail
          elsif visibility_change?
            properties[:page]
          elsif view? || submit? || change? || pagehide?
            nil
          else
            server_event_detail
          end
        end

        def click_text
          properties[:text]
        end

        private

          attr_reader :properties

          def page_label
            properties[:page].presence || "Unknown"
          end

          def click_label
            properties[:text].presence || "Click"
          end

          def submit_label
            label_with_optional_id("Submit", properties[:id])
          end

          def change_label
            verb = properties[:tag] == "select" ? "Select" : "Input"
            label_with_optional_id(verb, properties[:id])
          end

          def visibility_change_label
            properties[:state] == "hidden" ? "Leave" : "Return"
          end

          def click_detail
            if properties[:href].present?
              "to #{strip_origin(properties[:href])}"
            end
          end

          def server_event_detail
            (properties.keys - SERVER_DETAIL_IGNORED_KEYS)
              .map { "#{it}: #{properties[it]}" }
              .presence
              &.join(", ")
          end

          def label_with_optional_id(prefix, id)
            if id.present?
              "#{prefix} #{id}"
            else
              prefix
            end
          end

          def strip_origin(url)
            uri = URI.parse(url)
            app_host = Rails.application.routes.default_url_options[:host]
            if uri.host.nil? || uri.host == app_host
              uri.path
            else
              url
            end
          rescue URI::InvalidURIError
            url
          end
      end
    end
  end
end
