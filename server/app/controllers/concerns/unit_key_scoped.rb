module UnitKeyScoped
  extend ActiveSupport::Concern

  included do
    before_action :set_key
  end

  private
    def set_key
      @key = Unit::Key.find(params[:unit_key_id])
    end
end
