module Analytics
  class Visitor
    class Timeline
      module SessionMarkers
        MARKER_OFFSET = 0.001.seconds

        private
          def session_markers
            visits.flat_map { markers_for_visit(it) }
          end

          def markers_for_visit(visit)
            event_times = visit.ahoy_visit.events.filter_map(&:time)
            first_time = event_times.min || visit.started_at
            last_time = event_times.max || visit.started_at

            [
              Entry.session_start(
                time: first_time - MARKER_OFFSET,
                display_time: visit.started_at,
                duration: visit.visit_duration
              ),
              Entry.session_end(
                time: last_time + MARKER_OFFSET,
                display_time: last_time,
                duration: visit.visit_duration
              )
            ]
          end
      end
    end
  end
end
