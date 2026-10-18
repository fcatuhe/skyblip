require "test_helper"

class OwnshipsHelperTest < ActionView::TestCase
  test "a source links its addresses, a comma inside one included, and keeps the words around them" do
    html = ownship_source("https://en.wikipedia.org/wiki/A:_b,_c, span 11.23 m, after http://x.fr/plan.jpg")

    assert_dom_equal '<a href="https://en.wikipedia.org/wiki/A:_b,_c">https://en.wikipedia.org/wiki/A:_b,_c</a>, span 11.23 m, after <a href="http://x.fr/plan.jpg">http://x.fr/plan.jpg</a>', html
  end
end
