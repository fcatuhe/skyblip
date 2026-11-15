class Briefing::Devices::CapturesController < Briefing::ApplicationController
  skip_after_action :track_action
  rate_limit to: 10, within: 1.hour, with: -> { head :too_many_requests }

  def create
    capture = Capture.new(profile:, body:, device: params[:device_slug],
      browser: request.headers[Tracing::BROWSER_HEADER].presence)

    if capture.save
      render json: { kept_until: capture.kept_until.iso8601 }, status: :created
    else
      head :unprocessable_content
    end
  end

  private
    def profile = params.expect(capture: [ :profile ])[:profile]

    # INFO: fc 15nov26 one byte past the ceiling is enough to refuse, so a large upload is never read whole
    def body = params.expect(capture: [ :body ])[:body].read(Capture::MAX_BYTES + 1)
end
