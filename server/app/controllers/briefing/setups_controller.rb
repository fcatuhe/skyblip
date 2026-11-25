class Briefing::SetupsController < Briefing::ApplicationController
  PARTNER = /\A(?:go|bravo)-[0-9a-f]+\z/

  def show
    @partner = params[:for].to_s[PARTNER]
    @handoff = Briefing::Handoff.from(params)
  end
end
