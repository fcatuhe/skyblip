module Filterable
  extend ActiveSupport::Concern

  class_methods do
    def filter_with(filter_params)
      results = where(nil)
      filter_params.each do |attribute, value|
        value = value.reject(&:blank?) if value.is_a?(Array)
        results = results.public_send(:"filter_by_#{attribute}", value) if value.present?
      end
      results
    end
  end
end
