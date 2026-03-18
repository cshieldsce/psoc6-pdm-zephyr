cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(psoc6_pdm_example)

set(PDL_INC $ENV{ZEPHYR_BASE}/../modules/hal/infineon/mtb-pdl-cat1/devices/COMPONENT_CAT1A/include)
set(PDL_SRC $ENV{ZEPHYR_BASE}/../modules/hal/infineon/mtb-pdl-cat1/drivers/source)
set(PDL_DRV_INC $ENV{ZEPHYR_BASE}/../modules/hal/infineon/mtb-pdl-cat1/drivers/include)

target_sources(app PRIVATE
    src/main.c
    src/acoustic/acoustic.c
    src/acoustic/psoc6_pdm.c
    ${PDL_SRC}/cy_pdm_pcm.c
)

target_include_directories(app PRIVATE
    src
    src/acoustic
    ${PDL_INC}
    ${PDL_INC}/ip
    ${PDL_DRV_INC}
)