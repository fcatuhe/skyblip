class OAuth::AuthorizationsController < Admin::ApplicationController
  rescue_from OAuth::Error, with: :refuse

  def new
    if authorization.refusal
      redirect_to authorization.refused_url, allow_other_host: true
    else
      render locals: { authorization: }
    end
  end

  def create
    if authorization.refusal
      redirect_to authorization.refused_url, allow_other_host: true
    else
      redirect_to authorization.approved_url(Current.user), allow_other_host: true
    end
  end

  def destroy
    redirect_to authorization.denied_url, allow_other_host: true
  end

  private
    def authorization
      @authorization ||= OAuth::Authorization.new(params.permit(*OAuth::Authorization::FIELDS))
    end

    def refuse(error)
      render :refused, locals: { error: }, status: :bad_request
    end
end
