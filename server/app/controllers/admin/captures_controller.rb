class Admin::CapturesController < Admin::ApplicationController
  def index
    @captures = Capture.newest_first.without_body.limit(200)
  end

  def show
    capture = Capture.find(params[:id])
    send_data capture.download(by: Current.user), filename: capture.filename, type: "application/gzip"
  end
end
