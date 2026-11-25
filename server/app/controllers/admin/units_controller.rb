class Admin::UnitsController < Admin::ApplicationController
  def index
    @activated_only = params[:activated].present?
    units = @activated_only ? Unit.activated : Unit.all
    @units = units.includes(:active_key, :keys, :latest_firmware).newest_seen_first
    @sharing = Unit.where(id: Unit::Key.shared.select(:unit_id)).order(:serial).to_a
    @server_key = Identity::ServerKey.current if Identity::ServerKey.configured?
  end

  def show
    @unit = Unit.find(params[:id])
    @visitor_tokens = Ahoy::Visit.activating(@unit).distinct.pluck(:visitor_token)
  end
end
