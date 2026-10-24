require "test_helper"

class OwnshipTest < ActiveSupport::TestCase
  test "the firmware table round-trips through its own C++" do
    ownship = Ownship::AircraftType.find("glider").ownship
    again = Ownship.parse(ownship.to_cpp)

    assert_equal ownship, again
    assert_match "constexpr int kGlyphOriginRow = 4;", ownship.to_cpp
  end

  test "a < after a row marks the hot spot, wherever the widest row is" do
    assert_equal 4, Ownship::AircraftType.find("glider").ownship.hot_spot
    assert_equal 2, Ownship.parse("..##..\n######\n..##..  <").hot_spot
  end

  test "art reads rows of hashes and dots, padding short rows" do
    ownship = Ownship.parse("..##..\n.####\n######\n")

    assert_equal [ 6, 3 ], [ ownship.width, ownship.height ]
    assert_equal [ 0, 1, 1, 1, 1, 0 ], ownship.pixels[1]
  end

  # The comment block of skyship.cpp says 200x200, which must not parse as the word 0x200.
  test "a dimension in a comment is not a table word" do
    ownship = Ownship.parse("// 200x200, as drawn\nconstexpr int kGlyphWidth = 4;\n 0x6,\n 0xf,\n 0x6,\n")

    assert_equal 3, ownship.height
    assert_equal [ 0, 1, 1, 0 ], ownship.pixels.first
  end

  # A width or a row count past the limits is refused before a row is built, the page being open to anyone.
  test "an oversized glyph is refused before it is built" do
    assert_raises(Ownship::Unreadable) { Ownship.from_param(([ "1" ] * 200).join("."), width: "200000") }
    assert_raises(Ownship::Unreadable) { Ownship.parse("kGlyphWidth = 200000;\n" + ("0x1,\n" * 300)) }
    assert_raises(Ownship::Unreadable) { Ownship.from_param(([ "1" ] * 33).join("."), width: "8") }
  end

  test "a blank hot spot parameter is the widest row" do
    ownship = Ownship.from_param("00000002.00000007.00000002", width: "3", hot_spot: "")

    assert_equal 1, ownship.hot_spot
  end

  test "a parameter that does not fit its width is unreadable" do
    assert_raises(Ownship::Unreadable) { Ownship.from_param("000000ff", width: "4") }
    assert_raises(Ownship::Unreadable) { Ownship.from_param("", width: "4") }
    assert_raises(Ownship::Unreadable) { Ownship.from_param("1", width: "x") }
    assert_raises(Ownship::Unreadable) { Ownship.parse("nothing to see") }
  end

  test "the hot spot defaults to the widest row and ignores one off the glyph" do
    ownship = Ownship.parse("..##..\n######\n..##..\n")

    assert_equal 1, ownship.hot_spot
    assert_equal 1, Ownship.new(ownship.pixels, hot_spot: 9).hot_spot
    assert_equal 2, Ownship.new(ownship.pixels, hot_spot: 2).hot_spot
  end
end
