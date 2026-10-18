module Ownship::Checking
  extend ActiveSupport::Concern

  # INFO: fc 18oct26 the box of firmware/ui/widgets/skyship.h, which every screen is laid out around
  SPAN = 24
  ROWS = 16
  ROWS_TO_NOSE = 5

  Problem = Data.define(:key, :details)

  def problems
    [
      odd_width, too_wide, nose_too_long, tail_too_long,
      unmirrored, loose, detached, broken_spine
    ].compact
  end

  def fits_device? = problems.empty?

  def rows_aft = height - hot_spot

  private
    def odd_width
      problem(:odd_width, width:) if width.odd?
    end

    def too_wide
      problem(:too_wide, width:, max: SPAN) if width > SPAN
    end

    def nose_too_long
      problem(:nose_too_long, rows: hot_spot, max: ROWS_TO_NOSE) if hot_spot > ROWS_TO_NOSE
    end

    def tail_too_long
      problem(:tail_too_long, rows: rows_aft, max: ROWS - ROWS_TO_NOSE) if rows_aft > ROWS - ROWS_TO_NOSE
    end

    def unmirrored
      count = pixels.sum { |row| row.each_with_index.count { |bit, column| bit != row[width - 1 - column] } }
      problem(:unmirrored, count:) if count.positive?
    end

    def loose
      count = each_inked.count { |row, column| neighbours(row, column).zero? }
      problem(:loose, count:) if count.positive?
    end

    def detached
      inked = pixels.map { it.sum.positive? }
      first, last = inked.index(true), inked.rindex(true)
      problem(:detached) if first && inked[first..last].include?(false)
    end

    def broken_spine
      spine = width.even? ? [ width / 2 - 1, width / 2 ] : [ width / 2 ]
      count = pixels.count { |row| spine.any? { row[it].zero? } }
      problem(:broken_spine, count:) if count.positive?
    end

    def each_inked
      return enum_for(:each_inked) unless block_given?

      pixels.each_with_index do |row, r|
        row.each_with_index { |bit, c| yield r, c if bit == 1 }
      end
    end

    def neighbours(row, column)
      [ [ row - 1, column ], [ row + 1, column ], [ row, column - 1 ], [ row, column + 1 ] ].sum do |r, c|
        r.between?(0, height - 1) && c.between?(0, width - 1) ? pixels[r][c] : 0
      end
    end

    def problem(key, **details) = Problem.new(key:, details:)
end
