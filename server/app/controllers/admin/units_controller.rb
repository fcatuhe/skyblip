class Admin::UnitsController < Admin::ApplicationController
  def index
    @units = Unit.includes(:active_key, :keys, :latest_firmware).order(serial: :desc)
    @sharing = Unit.where(id: Unit::Key.shared.select(:unit_id)).order(:serial).to_a
    @server_key = Identity::ServerKey.current if Identity::ServerKey.configured?
  end

  def show
    @unit = Unit.find(params[:id])
  end
end
