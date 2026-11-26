class Briefing::SimulatorBuildsController < Briefing::ApplicationController
  def show
    @workbench = params[:mode] == "workbench"
    if !@workbench && Release.local_simulator
      @tag = Release::LOCAL_TAG
    elsif (@release = Release.simulator)
      @tag = @release.tag
    end
  rescue Release::Unavailable => error
    Rails.logger.warn(error.message)
    @unavailable = true
  end
end
