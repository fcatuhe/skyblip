class Briefing::SimulatorBuildsController < ApplicationController
  def show
    @release = Release.simulator
    @workbench = params[:mode] == "workbench"
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    @unavailable = true
  end
end
