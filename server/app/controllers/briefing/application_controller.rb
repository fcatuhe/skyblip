class Briefing::ApplicationController < ApplicationController
  allow_unauthenticated_access

  layout -> { turbo_frame_request? ? "turbo_rails/frame" : "briefing" }
end
