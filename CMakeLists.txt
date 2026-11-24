cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_PROJECT_NAME power_sampler_f411)
include("cmake/gcc-arm-none-eabi.cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)

set(HAL_SOURCES
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_spi.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_adc.c
    Core/Src/system_stm32f4xx.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c
    startup_stm32f411xe.s
)

set(APP_SOURCES
    Core/Src/main.c
    Core/Src/stm32f4xx_hal_msp.c
    Core/Src/stm32f4xx_it.c
    BSP/Src/bsp_gpio.c
    BSP/Src/bsp_timebase.c
    BSP/Src/bsp_adc_dma.c
    BSP/Src/bsp_spi_bus.c
    BSP/Src/bsp_i2c_bus.c
    BSP/Src/bsp_uart.c
    Driver/Src/drv_shunt_range.c
    Driver/Src/drv_ads1220.c
    Driver/Src/drv_eeprom_24c02.c
    Service/Src/svc_calib_store.c
    Service/Src/svc_sampler.c
    Service/Src/svc_host_proto.c
    App/Src/app_diag_cli.c
    App/Src/app_main.c
)

set(COMMON_INCLUDES
    Core/Inc
    BSP/Inc
    Driver/Inc
    Service/Inc
    App/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
)

set(COMMON_DEFINES
    USE_HAL_DRIVER
    STM32F411xE
)

add_library(stm32_hal OBJECT ${HAL_SOURCES})
target_include_directories(stm32_hal PUBLIC ${COMMON_INCLUDES})
target_compile_definitions(stm32_hal PUBLIC ${COMMON_DEFINES})
target_compile_options(stm32_hal PRIVATE -Wall -Wno-unused-parameter)

add_executable(${CMAKE_PROJECT_NAME} ${APP_SOURCES})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${COMMON_INCLUDES})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ${COMMON_DEFINES})
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wextra -Werror)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE stm32_hal)

set(OUTPUT_PATH ${CMAKE_BINARY_DIR}/output)
file(MAKE_DIRECTORY ${OUTPUT_PATH})

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${OUTPUT_PATH}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${OUTPUT_PATH}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT "Generating .hex and .bin firmware images"
)
