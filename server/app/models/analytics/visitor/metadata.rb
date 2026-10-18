module Analytics
  class Visitor
    module Metadata
      def metadata
        @metadata ||= {
          all_user_ids: all_user_ids,
          first_seen: first_visit&.started_at,
          last_seen: [ last_visit&.started_at, last_event_time ].compact.max,
          referrer: first_referrer,
          country: last_visit&.country,
          country_code: last_visit&.country_code,
          region: last_visit&.region,
          city: last_visit&.city,
          browser: last_visit&.browser,
          os: last_visit&.os,
          device: last_visit&.device_type
        }
      end

      private
        def first_visit
          @first_visit ||= analytics_visits.includes(:ahoy_visit).first
        end

        def last_visit
          @last_visit ||= analytics_visits.includes(:ahoy_visit).last
        end

        def all_user_ids
          @all_user_ids ||= analytics_visits.where.not(user_id: nil).distinct.pluck(:user_id)
        end

        def first_referrer
          first_visit&.referring_domain.presence || "Direct"
        end

        def last_event_time
          @last_event_time ||= Analytics.parse_time(
            Ahoy::Event.where(visit_id: analytics_visits.select(:ahoy_visit_id)).maximum(:time)
          )
        end
    end
  end
end
