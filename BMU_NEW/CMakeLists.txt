cmake_minimum_required(VERSION 3.22)

project(BMU_NEW C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(cmake/arm-none-eabi.cmake)

set(NTFX_PROJECT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(NTFX_DRIVER_DIR "${NTFX_PROJECT_DIR}/Driver")
set(NTFX_USER_DIR "${NTFX_PROJECT_DIR}/USER")
set(NTFX_LINKER_SCRIPT "${NTFX_DRIVER_DIR}/CMSIS/device/n32g45x_flash.ld")
set(APP_DIR "${CMAKE_CURRENT_LIST_DIR}/app")
set(APP_AFE_DIR "${APP_DIR}/AFE")
set(APP_DRIVER_DIR "${APP_DIR}/Drivers")

if(NOT EXISTS "${NTFX_PROJECT_DIR}/BMU_NEW.ntfx")
    message(FATAL_ERROR "NTFx generated project not found: ${NTFX_PROJECT_DIR}")
endif()

add_executable(${PROJECT_NAME}
    "${NTFX_DRIVER_DIR}/CMSIS/device/startup/startup_n32g45x_gcc.s"
    "${NTFX_DRIVER_DIR}/CMSIS/device/system_n32g45x.c"
    "${NTFX_USER_DIR}/src/main.c"
    "${NTFX_USER_DIR}/src/n32g45x_it.c"
    "${NTFX_USER_DIR}/src/n32g45x_cfg.c"
    "${APP_DIR}/bms_app.c"
    "${APP_DRIVER_DIR}/soft_i2c.c"
    "${APP_DRIVER_DIR}/syscalls.c"
    "${NTFX_DRIVER_DIR}/n32g45x_std_periph_driver/src/n32g45x_flash.c"
    "${NTFX_DRIVER_DIR}/n32g45x_std_periph_driver/src/n32g45x_dbg.c"
    "${NTFX_DRIVER_DIR}/n32g45x_std_periph_driver/src/n32g45x_rcc.c"
    "${NTFX_DRIVER_DIR}/n32g45x_std_periph_driver/src/misc.c"
    "${NTFX_DRIVER_DIR}/n32g45x_std_periph_driver/src/n32g45x_dma.c"
    "${NTFX_DRIVER_DIR}/n32g45x_std_periph_driver/src/n32g45x_gpio.c"
    "${NTFX_DRIVER_DIR}/n32g45x_std_periph_driver/src/n32g45x_usart.c"
    "${NTFX_DRIVER_DIR}/n32g45x_std_periph_driver/src/n32g45x_can.c"
    "${APP_AFE_DIR}/mp2797.c"
)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${APP_DIR}"
    "${APP_AFE_DIR}"
    "${APP_DRIVER_DIR}"
    "${NTFX_USER_DIR}/inc"
    "${NTFX_DRIVER_DIR}/CMSIS/core"
    "${NTFX_DRIVER_DIR}/CMSIS/device"
    "${NTFX_DRIVER_DIR}/n32g45x_std_periph_driver/inc"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    N32G45X
    USE_STDPERIPH_DRIVER
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfloat-abi=soft
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
)

target_link_options(${PROJECT_NAME} PRIVATE
    -mcpu=cortex-m4
    -mthumb
    -mfloat-abi=soft
    -T${NTFX_LINKER_SCRIPT}
    -Wl,-Map=${PROJECT_NAME}.map
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
)
