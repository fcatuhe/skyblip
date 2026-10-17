module Analytics
  class Visitor
    class Timeline
      module SourceFiltering
        private
          def source_events
            case source
            when "browser"
              browser_events
            when "server"
              server_events
            else
              all_events
            end
          end

          def browser_events
            all_events.select { it.name.start_with?("$") }
          end

          def server_events
            all_events.reject { it.name.start_with?("$") }
          end

          def all_events
            visits.flat_map { it.ahoy_visit.events }
          end
      end
    end
  end
end
