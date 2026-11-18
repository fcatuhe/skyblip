module DevicesHelper
  Photo = Data.define(:path, :width, :height, :top, :margins)
  Spot = Data.define(:x, :y, :radius, :from, :text, :anchor, :margin)
  Mark = Data.define(:spot, :label) do
    HEAD = 18

    def ring? = spot.radius.positive?

    def line_end
      along(length - (ring? ? spot.radius + 4 : HEAD))
    end

    def head
      base_x, base_y = line_end
      across_x, across_y = -direction[1] * HEAD / 2, direction[0] * HEAD / 2
      [ [ spot.x, spot.y ], [ base_x + across_x, base_y + across_y ], [ base_x - across_x, base_y - across_y ] ].map { it.map { it.round(1) }.join(",") }.join(" ")
    end

    private
      def length = Math.hypot(spot.x - spot.from[0], spot.y - spot.from[1])
      def direction = [ (spot.x - spot.from[0]) / length, (spot.y - spot.from[1]) / length ]
      def along(distance) = [ spot.from[0] + direction[0] * distance, spot.from[1] + direction[1] * distance ].map { it.round(1) }
  end

  PHOTOS = {
    go: Photo.new(path: "pages/index/skyblip-go-techo.avif", width: 419, height: 933, top: 440, margins: { left: 250, right: 200 }),
    bravo: Photo.new(path: "pages/skyblip-bravo/seeed-x1.avif", width: 440, height: 356, top: 0, margins: { left: 0, right: 0 })
  }.freeze

  # INFO: fc 18nov26 RST is the upper and the button the lower contact on the T-Echo's left side, out of view, as LilyGO's case label draws them
  SPOTS = {
    go: {
      rst: Spot.new(x: 24, y: 648, radius: 0, from: [ -60, 648 ], text: [ -70, 658 ], anchor: :end, margin: :left),
      button: Spot.new(x: 24, y: 862, radius: 0, from: [ -60, 862 ], text: [ -70, 872 ], anchor: :end, margin: :left),
      pad: Spot.new(x: 108, y: 551, radius: 0, from: [ 108, 486 ], text: [ 100, 474 ], anchor: :middle, margin: nil),
      lights: Spot.new(x: 281, y: 741, radius: 20, from: [ 470, 741 ], text: [ 480, 751 ], anchor: :start, margin: :right),
      usb: Spot.new(x: 343, y: 815, radius: 30, from: [ 470, 815 ], text: [ 480, 825 ], anchor: :start, margin: :right)
    },
    bravo: {
      button: Spot.new(x: 257, y: 124, radius: 46, from: [ 160, 56 ], text: [ 150, 52 ], anchor: :end, margin: nil),
      usb: Spot.new(x: 98, y: 263, radius: 22, from: [ 56, 318 ], text: [ 10, 342 ], anchor: :start, margin: nil)
    }
  }.freeze

  def device_photo(device) = PHOTOS.fetch(device)

  def device_marks(device, marks)
    marks.map { |spot, label| Mark.new(spot: SPOTS.fetch(device).fetch(spot), label:) }
  end

  def device_view_box(device, marks)
    photo = device_photo(device)
    margins = marks.map { it.spot.margin }
    left = margins.include?(:left) ? photo.margins[:left] : 0
    right = margins.include?(:right) ? photo.margins[:right] : 0
    [ -left, photo.top, left + photo.width + right, photo.height - photo.top ].join(" ")
  end
end
