class Admin::UnitsController < Admin::ApplicationController
  def index
    @units = Unit.includes(:active_key, :keys).order(serial: :desc)
    @server_keys = Activation::ServerKey.all
  end

  def show
    @unit = Unit.find(params[:id])
  end
end
