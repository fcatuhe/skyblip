class Admin::AgentsController < Admin::ApplicationController
  def index
    @grants = OAuth::Grant.includes(:user, :client).latest_first
  end

  def destroy
    OAuth::Grant.find(params[:id]).revoke
    redirect_to admin_agents_path, notice: "Agent disconnected.", status: :see_other
  end
end
