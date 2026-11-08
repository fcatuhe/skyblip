class Admin::UnitsController < Admin::ApplicationController
  def index
    @units = Unit.includes(:active_key, :keys).order(serial: :desc)
    @sharing = Unit.where(id: Unit::Key.shared.select(:unit_id)).order(:serial)
    @server_key = Identity::ServerKey.current if Identity::ServerKey.configured?
  end

  def show
    @unit = Unit.find(params[:id])
  end
end
