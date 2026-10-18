require "test_helper"

class Briefing::OwnshipsControllerTest < ActionDispatch::IntegrationTest
  test "the page hands the editor the type, its drawing, its outlines, the device's box and the words" do
    get briefing_ownship_path(type: "glider")

    assert_response :success
    assert_select "select#type option[selected][value=glider]"
    editor = css_select("[data-controller=ownship]").first
    assert_equal({ "key" => "glider", "code" => 4 }, JSON.parse(editor["data-ownship-type-value"]))
    assert_equal Ownship::AircraftType.find("glider").ownship.words, JSON.parse(editor["data-ownship-shipped-value"])["words"]
    assert_equal %w[js3], JSON.parse(editor["data-ownship-planforms-value"]).map { it["key"] }
    assert_equal({ "span" => 24, "rows" => 16, "rowsToNose" => 5 }, JSON.parse(editor["data-ownship-limits-value"]))
    assert_includes JSON.parse(editor["data-ownship-words-value"])["problems"].keys, "loose"
    assert_nil editor["data-ownship-linked-value"]
    assert_select "main script, main style, main [style], main [onclick]", count: 0
  end

  test "a drawing in the address is handed over as the linked one" do
    glider = Ownship::AircraftType.find("glider").ownship

    get briefing_ownship_path(type: "glider", w: 24, rows: glider.hex_rows.join("."), hot_spot: 4)

    linked = JSON.parse(css_select("[data-controller=ownship]").first["data-ownship-linked-value"])
    assert_equal({ "width" => 24, "words" => glider.words, "hotSpot" => 4 }, linked)
  end

  test "an unreadable drawing in the address says so and hands nothing over" do
    get briefing_ownship_path(type: "light_aircraft", rows: "zz", w: "24")

    assert_response :success
    assert_select "[role=alert]", text: /could not be read/
    assert_nil css_select("[data-controller=ownship]").first["data-ownship-linked-value"]
  end

  test "a parameter that is not a string is ignored" do
    get briefing_ownship_path(type: { x: "glider" }, rows: [ "1" ], outline: { y: "own" })

    assert_response :success
    assert_select "select#type option[selected][value=light_aircraft]"
  end

  test "each outline gets its own reference section with its sources linked" do
    get briefing_ownship_path(type: "light_aircraft")

    assert_select "select[data-ownship-target=outline] option", 4
    assert_select "section[data-outline=spitfire][hidden]" do
      assert_select "p", text: /Spitfire Mk IX: 11.2 m span/
      assert_select "a[href='https://commons.wikimedia.org/wiki/File:Supermarine_Spitfire_Mk_Vb_3-view.svg']"
    end
  end

  test "a type with no drawing starts from the light aircraft and says so" do
    get briefing_ownship_path(type: "balloon", locale: "fr")

    assert_response :success
    assert_select ".ownship-note", text: /type Ballon : l.appareil affiche celui du type Avion léger/
  end

  test "every type opens in both languages" do
    Ownship::AircraftType::KEYS.product(I18n.available_locales).each do |type, locale|
      get briefing_ownship_path(type:, locale: (locale unless locale == I18n.default_locale))
      assert_response :success, "#{type} in #{locale}"
    end
  end
end
