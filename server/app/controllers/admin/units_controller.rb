class Admin::UnitsController < Admin::ApplicationController
  def index
    @units = Unit.includes(:active_key, :keys).order(serial: :desc)
    @server_key = Identity::ServerKey.current if Identity::ServerKey.configured?
  end

  def show
    @unit = Unit.find(params[:id])
  end
end
