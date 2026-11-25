class Briefing::Setups::HandoffsController < Briefing::ApplicationController
  def show
    if (handoff = Briefing::Handoff.from(params))
      expires_in 1.day, public: true
      send_data handoff.code(briefing_setup_url(handoff.to_query)), type: "image/svg+xml", disposition: :inline
    else
      head :not_found
    end
  end
end
