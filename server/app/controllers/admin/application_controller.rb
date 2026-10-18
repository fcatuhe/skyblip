class Admin::ApplicationController < ApplicationController
  skip_around_action :switch_locale
  before_action :ensure_admin

  layout "admin"

  private
    def ensure_admin
      head :forbidden unless Current.user.admin?
    end
end
