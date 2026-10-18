require "application_system_test_case"

class Briefing::OwnshipsTest < ApplicationSystemTestCase
  test "a painted pixel reaches the checks, and its mirror twin with it" do
    visit "/briefing/ownship?type=glider"
    assert_text "Fits the device"

    find("[aria-label='Row 1, column 1']").click

    assert_text "2 loose px with no neighbour"
    assert_selector "[aria-label='Row 1, column 24']:checked"
    assert_current_path(/rows=00801801\./)
  end

  test "a dragged stroke paints every cell it crosses" do
    visit "/briefing/ownship?type=glider"
    from, to = center_of("Row 9, column 3"), center_of("Row 9, column 6")

    # INFO: fc 18oct26 by coordinates: each stroke rewrites the address, which drops Ferrum's node ids mid-drag
    page.driver.browser.mouse.move(**from).down.move(**to, steps: 6).up

    assert_selector ".ownship-row:nth-of-type(9) .ownship-cell:checked", count: 10
    assert_text "Fits the device"
  end

  test "a hot spot typed and entered moves the orange row and nothing else" do
    visit "/briefing/ownship?type=glider"

    find_field("Hot spot row").send_keys(:backspace, "6", :enter)

    assert_selector ".ownship-row--hot-spot [aria-label='Row 7, column 1']"
    assert_selector ".ownship-row", count: 11
  end

  test "an imported image sits under the grid, with no score and nothing to trace" do
    visit "/briefing/ownship?type=glider"

    attach_file "Import an image", file_fixture("own_outline.png")

    assert_current_path(/outline=own/)
    assert_selector "img.ownship-own-image[src^='data:image/png']"
    assert_no_button "trace the outline"
    assert_no_selector ".ownship-diff"
    assert_text "Your image stays in this browser"
  end

  test "choosing a type loads its drawing" do
    visit "/briefing/ownship?type=glider"

    select "Light aircraft", from: "Aircraft type"

    assert_selector ".ownship-row", count: 16
    assert_current_path(/type=light_aircraft&outline=menestrel/)
  end

  test "a drawing is a draft for its type, kept through a reload until it goes back to the shipped one" do
    visit "/briefing/ownship?type=glider"
    find("[aria-label='Row 9, column 5']").click
    assert_text "This is your drawing for Glider"

    visit "/briefing/ownship?type=glider"

    assert_selector "[aria-label='Row 9, column 5']:checked"
    click_on "back to the shipped symbol", match: :first
    assert_no_selector "[aria-label='Row 9, column 5']:checked"
    assert_no_text "This is your drawing for Glider"
  end

  test "a linked drawing leaves the draft alone until it is edited" do
    visit "/briefing/ownship?type=glider"
    find("[aria-label='Row 9, column 5']").click
    assert_text "This is your drawing for Glider"

    visit "/briefing/ownship?type=glider&w=24&rows=00001800.00001800.00ffffff.00001800"
    assert_selector ".ownship-row", count: 4
    assert_no_text "This is your drawing for Glider"
    visit "/briefing/ownship?type=glider"

    assert_selector "[aria-label='Row 9, column 5']:checked"
  end

  test "a drawing pasted as text replaces the one on the grid" do
    visit "/briefing/ownship?type=glider"

    fill_in "Drawing as text", with: "..##..\n######  <\n..##..\n..##.."
    click_on "Use this drawing"

    assert_selector ".ownship-row", count: 4
    assert_selector ".ownship-row--hot-spot [aria-label='Row 2, column 1']:checked"
    fill_in "Drawing as text", with: "not a drawing"
    click_on "Use this drawing"
    assert_text "That text is not a drawing"
  end

  private
    def center_of(label)
      x, y = page.evaluate_script("(() => { const r = document.querySelector(`[aria-label='#{label}']`).getBoundingClientRect(); return [r.x + r.width / 2, r.y + r.height / 2] })()")
      { x:, y: }
    end
end
