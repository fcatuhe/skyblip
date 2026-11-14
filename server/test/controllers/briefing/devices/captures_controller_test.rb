require "test_helper"

class Briefing::Devices::CapturesControllerTest < ActionDispatch::IntegrationTest
  test "a pilot's capture is kept under the device and the browser that sent it" do
    post briefing_device_captures_path(device_slug: "go-00012a"),
      params: { capture: { profile: "power_run", body: gzipped(capture_body) } },
      headers: { "X-Briefing-Browser" => "01AZ018Z" }

    assert_response :created
    capture = Capture.sole
    assert_equal [ "go-00012a", "01AZ018Z", "power_run" ], [ capture.device, capture.browser, capture.profile ]
    assert_equal capture.kept_until.iso8601, response.parsed_body["kept_until"]
  end

  test "a body that is no capture session is refused, and nothing is kept" do
    post briefing_device_captures_path(device_slug: "go-00012a"),
      params: { capture: { profile: "full", body: gzipped("not a capture") } }

    assert_response :unprocessable_content
    assert_equal 0, Capture.count
  end

  private
    def gzipped(bytes) = Rack::Test::UploadedFile.new(StringIO.new(bytes), "application/gzip", original_filename: "capture.ndjson.gz")
end
