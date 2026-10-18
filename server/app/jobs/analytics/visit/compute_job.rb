module Analytics
  class Visit
    class ComputeJob < ApplicationJob
      queue_as :ahoy
      queue_with_priority 10
      limits_concurrency to: 1, key: ->(ahoy_visit) { ahoy_visit }

      def perform(ahoy_visit)
        Analytics::Visit.compute_now(ahoy_visit)
      end
    end
  end
end
