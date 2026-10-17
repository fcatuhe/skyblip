module FirmwareShelfHelper
  FIRMWARE_SHELF = "firmware".freeze
  RELEASES = "https://github.com/fcatuhe/skyblip/releases/tag".freeze

  def firmware_shelf
    @firmware_shelf ||= begin
      shelf = Rails.public_path.join(FIRMWARE_SHELF, "shelf.json")
      shelf.exist? ? JSON.parse(shelf.read).fetch("images") : []
    end
  end

  def firmware_image_path(image)
    "/#{FIRMWARE_SHELF}/#{image.fetch("file")}"
  end

  def firmware_release_url(image)
    "#{RELEASES}/#{ERB::Util.url_encode(image.fetch("tag"))}"
  end
end
