module SimulatorHelper
  SIMULATOR_ROOT = "simulator".freeze

  GAUGE_RADIUS = 49
  GAUGE_MINOR = 4
  GAUGE_MAJOR = 8
  GAUGE_NUMERAL_INSET = 15
  GAUGE_BAND_RADIUS = 45
  GAUGE_INNER_BAND_RADIUS = 38
  GAUGE_CARDINALS = { 0 => "N", 90 => "E", 180 => "S", 270 => "W" }.freeze
  GAUGE_BANK_MARKS = [ -60, -30, -20, -10, 0, 10, 20, 30, 60 ].freeze

  VSI_FULL_FPM = 2000
  VSI_KNEE_FPM = 1000
  VSI_KNEE_DEG = 90
  VSI_OUTER_DEG = 80
  VSI_ZERO_DEG = -90

  SIMULATOR_DIALS = [
    { axis: "speed", area: "asi", label: "GS KT", min: 0, max: 180, step: 1, value: 120,
      scale: { zero: 180, span: 315, from: 0, to: 175, minor: 5, major: 25, numeral: 50 },
      bands: [ { tone: "white", from: 35, to: 75, radius: GAUGE_INNER_BAND_RADIUS },
               { tone: "green", from: 45, to: 140 },
               { tone: "yellow", from: 140, to: 165 },
               { tone: "red", from: 165, to: 180 } ] },
    { axis: "altitude", area: "alt", label: "ALT FT", min: 0, max: 18000, step: 100, value: 4500,
      scale: { zero: 0, span: 360, from: 0, to: 980, minor: 20, major: 100, numeral: 100,
               divisor: 100 },
      hands: 2 },
    { axis: "turn", area: "turn", label: "TURN D/S", min: -300, max: 300, step: 5, value: 0,
      marks: [ -110, -90, 90, 110 ], letters: { "L" => -70, "R" => 70 }, plane: true, ball: true },
    { axis: "slip", area: "turn", label: "SLIP MG", min: -250, max: 250, step: 5, value: 0 },
    { axis: "track", area: "trk", label: "TRK", min: 0, max: 359, step: 1, value: 225,
      scale: { zero: 0, span: 360, from: 0, to: 350, minor: 10, major: 30, numeral: 30,
               divisor: 10 },
      card: true, plane: true },
    { axis: "climb", area: "vsi", label: "VS FPM", min: -2000, max: 2000, step: 50, value: 0,
      scale: { law: :vsi, from: -2000, to: 2000, minor: 100, major: 500,
               numeral: 1000, divisor: 1000 } }
  ].freeze

  SIMULATOR_KEYS = [
    { keys: "&uarr; &darr;", dial: "VS" },
    { keys: "&larr; &rarr;", dial: "TURN" },
    { keys: "F S", dial: "GS" },
    { keys: "T", dial: "TFC" }
  ].freeze

  def simulator_build
    @simulator_build ||= Rails.public_path.join(SIMULATOR_ROOT).glob("*/embed.js").first&.dirname&.basename&.to_s
  end

  def simulator_module_path
    "/#{SIMULATOR_ROOT}/#{simulator_build}/embed.js" if simulator_build
  end

  def simulator_stylesheet_path
    "/#{SIMULATOR_ROOT}/#{simulator_build}/device.css" if simulator_build
  end

  def update_client_build
    @update_client_build ||= Rails.public_path.join(SIMULATOR_ROOT).glob("*/update.js").first&.dirname&.basename&.to_s
  end

  def update_module_path
    "/#{SIMULATOR_ROOT}/#{update_client_build}/update.js" if update_client_build
  end

  def gauge_marks(dial)
    return fixed_marks(dial) unless dial[:scale]

    scale = dial[:scale]
    scale_values(scale, scale[:minor]).map do |value|
      major = (value % scale[:major]).zero?
      mark_at(scale_angle(scale, value), major ? GAUGE_MAJOR : GAUGE_MINOR) + [ major ]
    end
  end

  def gauge_numerals(dial)
    return lettered_marks(dial) unless dial[:scale]

    scale = dial[:scale]
    scale_values(scale, scale[:numeral]).map do |value|
      [ numeral_text(dial, value) ] + gauge_point(scale_angle(scale, value),
                                                  GAUGE_RADIUS - GAUGE_NUMERAL_INSET)
    end
  end

  def gauge_bands(dial)
    Array(dial[:bands]).map do |band|
      radius = band[:radius] || GAUGE_BAND_RADIUS
      [ band[:tone], arc_path(scale_angle(dial[:scale], band[:from]),
                              scale_angle(dial[:scale], band[:to]), radius) ]
    end
  end

  def self.vsi_angle(fpm)
    rate = [ fpm.abs, VSI_FULL_FPM ].min
    inner = [ rate, VSI_KNEE_FPM ].min * VSI_KNEE_DEG / VSI_KNEE_FPM
    outer = [ rate - VSI_KNEE_FPM, 0 ].max * VSI_OUTER_DEG / (VSI_FULL_FPM - VSI_KNEE_FPM)
    VSI_ZERO_DEG + (fpm.negative? ? -1 : 1) * (inner + outer)
  end

  def gauge_bank_marks
    GAUGE_BANK_MARKS.map { |angle| mark_at(angle, angle.abs >= 30 ? GAUGE_MAJOR : GAUGE_MINOR) }
  end

  private
    def scale_values(scale, step)
      (scale[:from]..scale[:to]).step(step).to_a
    end

    def scale_angle(scale, value)
      return SimulatorHelper.vsi_angle(value) if scale[:law] == :vsi

      pivot = scale.fetch(:pivot, scale[:from])
      scale[:zero] + ((value - pivot) * scale[:span]).fdiv(scale[:to] - scale[:from])
    end

    def numeral_text(dial, value)
      cardinal = GAUGE_CARDINALS[value % 360] if dial[:card]
      cardinal || (value.abs / dial[:scale].fetch(:divisor, 1)).round.to_s
    end

    def fixed_marks(dial)
      Array(dial[:marks]).map { |angle| mark_at(angle, GAUGE_MAJOR) + [ true ] }
    end

    def lettered_marks(dial)
      dial.fetch(:letters, {}).map do |letter, angle|
        [ letter ] + gauge_point(angle, GAUGE_RADIUS - GAUGE_NUMERAL_INSET)
      end
    end

    def mark_at(angle, length)
      gauge_point(angle, GAUGE_RADIUS) + gauge_point(angle, GAUGE_RADIUS - length)
    end

    def arc_path(from_angle, to_angle, radius)
      x1, y1 = gauge_point(from_angle, radius)
      x2, y2 = gauge_point(to_angle, radius)
      "M#{x1} #{y1} A#{radius} #{radius} 0 #{(to_angle - from_angle).abs > 180 ? 1 : 0} 1 #{x2} #{y2}"
    end

    def gauge_point(angle, radius)
      radians = angle * Math::PI / 180
      [ (radius * Math.sin(radians)).round(2), (-radius * Math.cos(radians)).round(2) ]
    end
end
