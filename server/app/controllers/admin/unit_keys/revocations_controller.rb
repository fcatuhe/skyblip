class Admin::UnitKeys::RevocationsController < Admin::ApplicationController
  include UnitKeyScoped

  def create
    if @key.active?
      @key.revoked!
      redirect_to admin_unit_path(@key.unit), notice: "Key revoked: the next registration binds its key."
    else
      redirect_to admin_unit_path(@key.unit), alert: "Only the active key can be revoked."
    end
  end
end
