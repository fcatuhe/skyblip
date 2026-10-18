module Analytics
  class Overview
    class Chart
      WIDTH = 100
      HEIGHT = 100

      Line = Data.define(:name, :points)
      Mark = Data.define(:time, :x, :values)

      def initialize(series, hourly:)
        @series = series
        @hourly = hourly
      end

      def hourly? = @hourly

      def any? = peak.positive?

      def view_box = "0 0 #{WIDTH} #{HEIGHT}"

      def lines
        @lines ||= @series.map { |name, values| Line.new(name:, points: polyline_of(values)) }
      end

      def axis = [ scale, scale.fdiv(2), 0 ]

      def marks
        @marks ||= periods.map.with_index do |period, index|
          Mark.new(time: time_of(period), x: x_at(index),
                   values: @series.transform_values { it[period].to_i })
        end
      end

      def band = Percentage.of(1, columns)

      private
        def periods = @periods ||= @series.values.flat_map(&:keys).uniq.sort

        def single_period? = periods.one?

        def columns = [ periods.size - 1, 1 ].max

        def x_at(index) = single_period? ? WIDTH.fdiv(2) : Percentage.of(index, columns)

        def y_of(value) = HEIGHT - Percentage.of(value, scale)

        def polyline_of(values)
          heights = periods.map { y_of(values[it].to_i) }

          if single_period?
            "0,#{heights.first} #{WIDTH},#{heights.first}"
          else
            heights.map.with_index { |height, index| "#{x_at(index)},#{height}" }.join(" ")
          end
        end

        def scale = @scale ||= Scale.ceiling(peak)

        def peak = @peak ||= @series.values.flat_map(&:values).map(&:to_i).max.to_i

        def time_of(period) = Time.find_zone("UTC").parse(period)
    end
  end
end
