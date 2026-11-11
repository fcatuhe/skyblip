# Every image built for this board links board.c, MCUboot included, because MCUboot reads slot1 off the NOR it powers.
zephyr_library()
zephyr_library_sources(board.c)
