class Admin::Analytics::VisitorsController < Admin::ApplicationController
  def index
    visitors = Analytics::Visitors.new(filter_params: analytics_filter_params)
    @filter_params = visitors.filter_params
    @visitors = visitors.page(params[:page])
    @users = User.where(id: @visitors.summaries.filter_map(&:user_id)).index_by(&:id)
  end

  def show
    @visitor = Analytics::Visitor.new(visitor_token: params[:visitor_token], **visitor_params)
    @filter_params = analytics_filter_params
  end

  private
    def visitor_params
      { source: params[:source], page: params[:page] }.compact
    end

    def analytics_filter_params
      params.fetch(:filters, {}).permit(*Analytics::FILTER_KEYS)
    end
end
