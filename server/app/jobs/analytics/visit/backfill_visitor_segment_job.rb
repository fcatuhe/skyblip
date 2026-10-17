module Analytics
  class Visit
    class BackfillVisitorSegmentJob < ApplicationJob
      queue_as :ahoy
      queue_with_priority 10

      def perform(visitor_token)
        Ahoy::Visit.where(visitor_token:).find_each do |ahoy_visit|
          Analytics::Visit.compute_later(ahoy_visit)
        end
      end
    end
  end
end
