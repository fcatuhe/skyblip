class Admin::ActivationCapsController < Admin::ApplicationController
  before_action :set_cap

  def edit
  end

  def update
    if @cap.update(cap_params)
      redirect_to admin_units_path, notice: "Daily cap updated."
    else
      render :edit, status: :unprocessable_content
    end
  end

  private
    def set_cap
      @cap = Activation::Cap.current
    end

    def cap_params
      params.expect(activation_cap: %i[new_units_per_day])
    end
end
