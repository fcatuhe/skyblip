class ApplicationController < ActionController::Base
  include Authentication
  include Localized

  # Only allow modern browsers supporting webp images, web push, badges, import maps, CSS nesting, and CSS :has.
  # allow_browser versions: :modern

  # Changes to the importmap will invalidate the etag for HTML responses
  stale_when_importmap_changes

  after_action :track_action, unless: -> { Ahoy.api_only || prefetch? }

  private
    def track_action
      ahoy.track [ controller_name, action_name ].join("#"), request.path_parameters
    end

    def prefetch? = request.headers["X-Sec-Purpose"] == "prefetch"
end
