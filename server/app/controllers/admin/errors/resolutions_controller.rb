class Admin::Errors::ResolutionsController < Admin::ApplicationController
  before_action :set_error

  def create
    @error.resolve(by: Current.user)
    redirect_to admin_error_path(@error.fingerprint), notice: "Resolved. It reopens if it happens again."
  end

  def destroy
    @error.reopen(by: Current.user)
    redirect_to admin_error_path(@error.fingerprint), notice: "Reopened."
  end

  private
    def set_error
      @error = Event::Error.find_by(fingerprint: params[:error_fingerprint]) || raise(ActiveRecord::RecordNotFound)
    end
end
