module Analytics
  class Visit
    class PurgeNonBrowserVisitsJob < ApplicationJob
      def perform(older_than: 7.days.ago)
        visits = Ahoy::Visit.where(started_at: ...older_than).where.missing(:analytics_visit)

        Ahoy::Event.where(visit_id: visits.select(:id)).delete_all
        visits.delete_all
      end
    end
  end
end
