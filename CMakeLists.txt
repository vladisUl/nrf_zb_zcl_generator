#
# Copyright (c) 2020 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)
list(APPEND ZEPHYR_EXTRA_MODULES "E:/ncs_workspaces/ncs-3.2.3/ncs-zigbee")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zigbee)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/zb_zcl_struct.c
  src/zephyr_drivers.c
)

target_include_directories(app PRIVATE include)

