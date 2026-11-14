class Admin::CapturesController < Admin::ApplicationController
  def index
    @captures = Capture.newest_first.without_body.limit(200)
  end

  def show
    capture = Capture.find(params[:id])
    Rails.event.notify "capture.downloaded", by_id: Current.user.id, capture_id: capture.id, device: capture.device
    send_data capture.body, filename: capture.filename, type: "application/gzip"
  end
end
