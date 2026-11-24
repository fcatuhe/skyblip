require "test_helper"

class BriefingWordsTest < ActiveSupport::TestCase
  FIRMWARE = Rails.root.join("../firmware")
  CONFIG = "products/common/services"
  REGISTRY = JSON.parse(Rails.root.join("../schemas/link.v1.json").read)

  # INFO: fc 11nov26 what the device answers but the page never asks, so no pilot ever reads it
  NEVER_ASKED = %w[no_command no_version invalid].freeze
  UPDATE_NOTICES = %w[
    claimed window_closed older wrong_key wrong_product unmarked_image too_large not_image flash smp failed refused
    timeout link_lost not_back not_skyblip no_bluetooth no_file no_params shelf_failed needs_full recovering
    probation reverted image_refused settings_prior settings_defaults swap_unpowered went_dark_flat imu_writing not_activated
  ].freeze
  # INFO: fc 12nov26 the page offers the registry's profiles by name, so it never sends one past stop
  NEVER_SENT = %w[out_of_range].freeze
  CAPTURE_NOTICES = %w[capture_armed capture_stopped expired cancelled prompt_pending refused_on_device].freeze
  SET_REFUSALS = %w[in_flight low_power expired cancelled prompt_pending refused_on_device small_mtu unknown_command out_of_range not_own_transponder set_on_go].freeze
  PROMPT_REFUSALS = %w[expired cancelled prompt_pending refused_on_device claimed small_mtu].freeze
  SERVER_REFUSALS = %w[invalid unknown_server_key expired bad_signature other_key revoked_key daily_cap network_cap no_serial rate_limited unreachable failed].freeze
  ACTIVATION_NOTICES = %w[activated no_bluetooth not_skyblip link_lost failed refused].freeze
  # INFO: fc 13nov26 a forget the unit never answers is a timeout, which trust.js names
  BROWSER_NOTICES = %w[untrusted invalid timeout].freeze
  # INFO: fc 17nov26 the page asks an untrusted Bravo nothing and names a tag in range, and tango.js times out a silent Go
  PAIR_NEVER_ASKED = %w[untrusted out_of_range].freeze
  PAIR_NOTICES = %w[timeout].freeze
  GO_PAIRING = Rails.root.join("../firmware/products/skyblip_go/services/bravo.cpp")
  SETUP_NOTICES = %w[
    port_closed port_failed port_busy port_busy_linux other_board not_drive drive_denied drive_failed download_failed no_image
    image_not_uf2 image_wrong_product image_no_image image_no_room write_failed failed
  ].freeze
  INSTALL_CAUSES = Rails.root.join("app/javascript/skyblip/install_report.js").read[/INSTALL_CAUSES = \[(.*?)\]/, 1].scan(/'(\w+)'/).flatten

  test "the firmware still refuses with the reasons this test reads" do
    assert_includes install_refusals, "not_approved"
    assert_includes install_refusals, "small_mtu"
    assert_includes refusals("#{CONFIG}/config_capture.cpp", "refuse\\("), "not_capturing"
    assert_includes refusals("#{CONFIG}/config_identity.cpp", "refuse\\("), "bad_signature"
  end

  I18n.available_locales.each do |locale|
    test "every refusal and notice of an install has words in #{locale}" do
      keys = install_refusals - NEVER_ASKED + UPDATE_NOTICES
      assert_empty keys - words(locale, :notices)
    end

    test "every cause an install report names has words in #{locale}" do
      assert_includes INSTALL_CAUSES, "no_record"
      assert_empty INSTALL_CAUSES - I18n.t("briefing.notices.install_report", locale:).keys.map(&:to_s)
    end

    test "every refusal of a settings change has words in #{locale}" do
      assert_empty SET_REFUSALS - (words(locale, :set_notices) + words(locale, :notices))
      assert_includes words(locale, :set_notices), "saved"
    end

    test "every refusal and verdict of a capture has words in #{locale}" do
      keys = refusals("#{CONFIG}/config_capture.cpp", "refuse\\(") - NEVER_ASKED - NEVER_SENT + CAPTURE_NOTICES
      assert_empty keys - words(locale, :capture_notices)
    end

    test "every refusal of a forget has words in #{locale}" do
      keys = refusals("#{CONFIG}/config_browser.cpp", "acknowledge\\(false, ") + BROWSER_NOTICES
      assert_empty keys - words(locale, :browsers_notices)
    end

    test "the captures are offered under the registry's names in #{locale}" do
      assert_equal enum("capture_profile") - %w[stop], I18n.t("briefing.capture.profile_names", locale:).keys.map(&:to_s)
    end

    test "every refusal of an activation has words in #{locale}" do
      keys = refusals("#{CONFIG}/config_identity.cpp", "refuse\\(") + PROMPT_REFUSALS + SERVER_REFUSALS.map { "server_#{it}" } + ACTIVATION_NOTICES
      assert_empty keys - words(locale, :activation_notices)
    end

    test "every refusal and verdict of a pairing has words in #{locale}" do
      verdicts = GO_PAIRING.read.scan(/return link::Reason::(\w+);/).flatten.map(&:underscore)
      keys = refusals("#{CONFIG}/config_pair.cpp", "refuse\\(") - NEVER_ASKED - PAIR_NEVER_ASKED + verdicts + PROMPT_REFUSALS + PAIR_NOTICES
      assert_includes verdicts, "not_found"
      assert_empty keys - words(locale, :pair_notices) - words(locale, :notices)
    end

    test "every notice of a USB install has words in #{locale}" do
      assert_empty SETUP_NOTICES - words(locale, :setup_notices)
    end

    test "the units and the alarm steps are offered under the registry's names in #{locale}" do
      assert_equal enum("units"), I18n.t("briefing.settings.unit_names", locale:).keys.map(&:to_s)
      assert_equal enum("alarm"), I18n.t("briefing.settings.alarm_names", locale:).keys.map(&:to_s)
    end
  end

  test "the installer still names every notice this test words" do
    source = %w[installer.js uf2.js].sum("") { Rails.root.join("app/javascript/skyblip", it).read } + Rails.root.join("app/javascript/controllers/setup_controller.js").read
    SETUP_NOTICES.each { |key| assert_match(/["']#{key.delete_prefix("image_")}["']|setup_notices\.#{key}/, source, key) }
  end

  test "every word the pages ask for exists in both languages" do
    asked = Rails.root.glob("app/javascript/controllers/**/*.js").flat_map { it.read.scan(/\bsay(?:Count)?\("([\w.]+)"/).flatten }.uniq
    assert_operator asked.size, :>, 20
    I18n.available_locales.each do |locale|
      missing = asked.reject { |path| I18n.exists?("briefing.#{path}", locale) }
      assert_empty missing, locale
    end
  end

  test "both languages word the same things" do
    english, french = I18n.available_locales.map { |locale| keys_of(I18n.t("briefing", locale:)) }
    assert_equal english.sort, french.sort
  end

  private
    def words(locale, scope) = I18n.t("briefing.#{scope}", locale:).keys.map(&:to_s)

    def enum(name) = REGISTRY.dig("enums", name, "values").map { it["name"] }

    def install_refusals
      %W[#{CONFIG}/config_service.cpp core/comms/config.h].sum([]) { refusals(it, "(?:acknowledge\\(false, |refuse_landing\\()") }.uniq
    end

    def refusals(file, call)
      reasons = enum("reason").index_by { it.camelize }
      FIRMWARE.join(file).read.gsub(%r{//.*$}, "").scan(/#{call}link::Reason::(\w+)\)/).flatten.uniq.map do |enumerator|
        reasons.fetch(enumerator) { flunk "#{file} refuses with link::Reason::#{enumerator}, which the registry does not name" }
      end
    end

    def keys_of(tree, prefix = nil)
      tree.flat_map do |key, value|
        path = [ prefix, key ].compact.join(".")
        value.is_a?(Hash) ? keys_of(value, path) : [ path ]
      end
    end
end
