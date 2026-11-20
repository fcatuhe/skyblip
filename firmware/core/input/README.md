# core/input

`Contact` debounces a sampled level and emits one `Down` and one `Up` per press.

The board passes the settle window at construction (`boards/lilygo/t_echo_plus/pins.h`), because a button's bounce and a capacitive pad's flutter differ from board to board.

What a press means belongs to the product (for skyBlip Go, `products/skyblip_go/input/`). The board instantiates the conditioner, so it sits below `boards/`, and the hold thresholds stay out of the board's reach.

`edge_ms()` is when the level changed, not when the poll saw it, so a hold is measured from the touch itself.
