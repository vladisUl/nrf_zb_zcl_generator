# SPDX-License-Identifier: Apache-2.0

#board_set_flasher(jlink)
board_set_flasher(nrfjprog)
board_set_debugger(jlink)

board_runner_args(jlink "--device=nRF52840_xxAA" "--speed=4000")
board_runner_args(pyocd "--target=nrf52840" "--frequency=4000000")

#include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfjprog.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)