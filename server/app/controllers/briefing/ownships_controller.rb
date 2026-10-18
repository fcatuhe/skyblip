class Briefing::OwnshipsController < ApplicationController
  allow_unauthenticated_access

  def show
    @aircraft_type = Ownship::AircraftType.find(params[:type])
    @linked = linked_ownship
  end

  private
    def linked_ownship
      if params[:rows].is_a?(String)
        Ownship.from_param(params[:rows], width: params[:w].to_s, hot_spot: params[:hot_spot].to_s)
      end
    rescue Ownship::Unreadable
      @unreadable = true
      nil
    end
end
