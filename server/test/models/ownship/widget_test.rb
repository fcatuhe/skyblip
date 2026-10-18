require "test_helper"

class Ownship::WidgetTest < ActiveSupport::TestCase
  setup do
    @copy = Rails.root.join("tmp/skyship_#{SecureRandom.hex(4)}.cpp")
    FileUtils.cp(Ownship::Widget::PATH, @copy)
    @widget = Ownship::Widget.new(@copy)
  end

  teardown { FileUtils.rm_f(@copy) }

  test "the light aircraft drawing is the symbol the firmware compiles in" do
    assert_equal Ownship::AircraftType.default.ownship, Ownship::Widget.new.ownship
    assert_not @widget.compile(Ownship::AircraftType.default.ownship)
  end

  test "compiling a drawing rewrites the table and leaves the rest of the file" do
    rows = Ownship::AircraftType.default.ownship.pixels.map(&:dup)
    rows[15] = Array.new(24, 0).tap { it[11] = it[12] = 1 }
    rows[14] = rows[14].map.with_index { |_, c| c.between?(10, 13) ? 1 : 0 }
    redrawn = Ownship.new(rows, hot_spot: 5)
    before = @copy.read

    assert @widget.compile(redrawn)
    assert_equal redrawn, @widget.ownship
    assert_equal before.sub(Ownship::Widget::TABLE, ""), @copy.read.sub(Ownship::Widget::TABLE, "")
  end

  test "a drawing that does not fit the device is refused" do
    assert_raises(Ownship::Widget::Unfit) { @widget.compile(Ownship.parse("#..##..#\n..####..\n...##...")) }
    assert_equal Ownship::AircraftType.default.ownship, @widget.ownship
  end

  # skyship.cpp static_asserts the compiled symbol's size and hot spot, so the glider, which fits the device, still cannot be compiled.
  test "a drawing of another size than the compiled symbol is refused" do
    error = assert_raises(Ownship::Widget::Unfit) { @widget.compile(Ownship::AircraftType.find("glider").ownship) }

    assert_match "this one is 24 by 11, hot spot on row 4, problems: none", error.message
  end
end
