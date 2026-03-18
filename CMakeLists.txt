cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(psoc6_pdm_example VERSION 1.0.0)

add_subdirectory(app)
