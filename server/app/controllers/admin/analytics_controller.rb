class Admin::AnalyticsController < Admin::ApplicationController
  def show
    @overview = Analytics::Overview.new(filter_params: analytics_filter_params)
    @filter_params = @overview.filter_params
  end

  private
    def analytics_filter_params
      params.fetch(:filters, {}).permit(*Analytics::FILTER_KEYS)
    end
end
