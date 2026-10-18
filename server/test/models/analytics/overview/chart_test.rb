require "test_helper"

class Analytics::Overview::ChartTest < ActiveSupport::TestCase
  test "a line spans the full width and is inverted against the axis top" do
    chart = chart_of({ visits: { "2026-01-01" => 0, "2026-01-02" => 5, "2026-01-03" => 10 } })

    assert_equal [ 10, 5.0, 0 ], chart.axis
    assert_equal "0.0,100.0 50.0,50.0 100.0,0.0", chart.lines.sole.points
  end

  # The widest series is drawn first so the narrower lines land on top of it where they coincide.
  test "lines keep the order of the series they come from" do
    chart = chart_of({ pageviews: { "2026-01-01" => 9 }, visits: { "2026-01-01" => 3 } })

    assert_equal %i[pageviews visits], chart.lines.map(&:name)
  end

  test "every line shares the axis of the tallest series" do
    chart = chart_of({ visitors: { "2026-01-01" => 1 }, pageviews: { "2026-01-01" => 7 } })

    assert_equal [ 10, 5.0, 0 ], chart.axis
    # 1 and 7 against a shared scale of 10, so 90% and 30% down from the top
    assert_equal [ "0,90.0 100,90.0", "0,30.0 100,30.0" ], chart.lines.map(&:points)
  end

  # The hover band is the full width when there is one bucket, so the mark has to sit in the middle of it.
  test "a single period draws a flat line across the plot and centres its mark" do
    chart = chart_of({ visits: { "2026-01-01" => 3 } })

    assert_equal "0,0.0 100,0.0", chart.lines.sole.points
    assert_equal 50.0, chart.marks.sole.x
    assert_equal 100.0, chart.band
  end

  test "an empty series charts nothing rather than dividing by zero" do
    chart = chart_of({ visits: {} })

    assert_not chart.any?
    assert_equal [ 0, 0.0, 0 ], chart.axis
    assert_empty chart.marks
  end

  test "a mark carries the bucket time and every series value under it" do
    chart = chart_of({ visitors: { "2026-01-01" => 1, "2026-01-02" => 2 },
                       visits: { "2026-01-02" => 7 } })

    first, last = chart.marks

    assert_equal Time.utc(2026, 1, 1), first.time
    assert_equal({ visitors: 1, visits: 0 }, first.values)
    assert_equal 100.0, last.x
    assert_equal({ visitors: 2, visits: 7 }, last.values)
  end

  test "hourly buckets keep their hour" do
    chart = chart_of({ visits: { "2026-01-01 14:00:00" => 1 } }, hourly: true)

    assert chart.hourly?
    assert_equal Time.utc(2026, 1, 1, 14), chart.marks.sole.time
  end

  private
    def chart_of(series, hourly: false)
      Analytics::Overview::Chart.new(series, hourly:)
    end
end
