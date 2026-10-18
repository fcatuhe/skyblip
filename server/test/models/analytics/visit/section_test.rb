require "test_helper"

class Analytics::Visit::SectionTest < ActiveSupport::TestCase
  test "reads the section off the controllers the server tracked" do
    assert_equal :admin, section(server("admin/users"), view("/admin/users"))
    assert_equal :admin, section(server("mission_control/jobs/jobs"))
    assert_equal :admin, section(server("sessions"))
    assert_equal :briefing, section(server("briefing/consoles"))
    assert_equal :public, section(server("content/pages"))
  end

  test "an admin page anywhere in the visit makes it an admin visit" do
    assert_equal :admin, section(server("content/pages"), server("briefing/firmwares"), server("admin/analytics"))
    assert_equal :briefing, section(server("content/pages"), server("briefing/firmwares"))
  end

  test "falls back on the pages the browser viewed" do
    assert_equal :admin, section(view("/admin/analytics"))
    assert_equal :admin, section(view("/session/new"))
    assert_equal :briefing, section(view("/briefing/simulator"))
    assert_equal :briefing, section(view("/fr/briefing"))
    assert_equal :public, section(view("/briefings-and-more"))
    assert_equal :public, section(view("/fr/guides"))
  end

  private
    def section(*events) = Analytics::Visit::Section.new(events).name

    def server(controller) = Ahoy::Event.new(name: "#{controller}#show", properties: { "controller" => controller })

    def view(page) = Ahoy::Event.new(name: "$view", properties: { "page" => page })
end
