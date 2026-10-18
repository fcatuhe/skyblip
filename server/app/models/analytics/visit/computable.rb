module Analytics
  class Visit
    module Computable
      extend ActiveSupport::Concern

      class_methods do
        def compute_later(ahoy_visit)
          ComputeJob.perform_later(ahoy_visit)
        end

        def compute_now(ahoy_visit)
          Computation.new(ahoy_visit).save!
        end
      end
    end
  end
end
