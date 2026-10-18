class Admin::UnitKeys::PromotionsController < Admin::ApplicationController
  include UnitKeyScoped

  def create
    if @key.pending?
      @key.promote
      redirect_to admin_unit_path(@key.unit), notice: "Key promoted, the previous one revoked."
    else
      redirect_to admin_unit_path(@key.unit), alert: "Only a pending key can be promoted."
    end
  end
end
