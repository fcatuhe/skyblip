require "test_helper"

class Analytics::Visitor::Timeline::EntryTest < ActiveSupport::TestCase
  # --- label ---

  test "label for $view" do
    assert_equal "/home", entry("$view", page: "/home").label
    assert_equal "Unknown", entry("$view", page: nil).label
  end

  test "label for $click" do
    assert_equal "Sign up", entry("$click", text: "Sign up").label
    assert_equal "Click", entry("$click").label
  end

  test "label for $submit" do
    assert_equal "Submit contact-form", entry("$submit", id: "contact-form").label
    assert_equal "Submit", entry("$submit").label
  end

  test "label for $change" do
    assert_equal "Select country", entry("$change", tag: "select", id: "country").label
    assert_equal "Input user_email", entry("$change", tag: "input", id: "user_email").label
  end

  test "label for $visibilitychange" do
    assert_equal "Leave", entry("$visibilitychange", state: "hidden").label
    assert_equal "Return", entry("$visibilitychange", state: "visible").label
  end

  test "label for $pagehide" do
    assert_equal "Close", entry("$pagehide").label
  end

  test "label for custom event" do
    assert_equal "signup", entry("signup", plan: "pro").label
  end

  # --- detail ---

  test "detail for $click with href" do
    assert_equal "to /signup", entry("$click", href: "/signup").detail
  end

  test "detail for $click without href" do
    assert_nil entry("$click").detail
  end

  test "detail for $visibilitychange" do
    assert_equal "/home", entry("$visibilitychange", state: "hidden", page: "/home").detail
  end

  test "detail for custom event" do
    assert_equal "plan: pro", entry("signup", plan: "pro").detail
  end

  test "detail excludes controller and action" do
    assert_nil entry("signup", controller: "users", action: "create").detail
  end

  # --- queries ---

  test "session_start?" do
    entry = Analytics::Visitor::Timeline::Entry.session_start(time: Time.current, display_time: Time.current, duration: 0)
    assert entry.session_start?
    assert entry.session_marker?
    assert_not entry.session_end?
  end

  test "session_end?" do
    entry = Analytics::Visitor::Timeline::Entry.session_end(time: Time.current, display_time: Time.current, duration: 0)
    assert entry.session_end?
    assert entry.session_marker?
    assert_not entry.session_start?
  end

  test "view?" do
    assert entry("$view", page: "/").view?
    assert_not entry("$click").view?
  end

  test "click?" do
    assert entry("$click").click?
    assert_not entry("$view", page: "/").click?
  end

  private

    def entry(type, **properties)
      Analytics::Visitor::Timeline::Entry.new(
        type: type,
        time: Time.current,
        properties: properties.with_indifferent_access
      )
    end
end
