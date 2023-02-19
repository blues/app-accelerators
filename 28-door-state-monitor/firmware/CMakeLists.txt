# Copyright 2022 Blues Inc.  All rights reserved.
# Use of this source code is governed by licenses granted by the
# copyright holder including that found in the LICENSE file.

# Set CMake policy behavior
cmake_minimum_required(VERSION 3.14)

# Use the custom ARM toolchain
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/arm-gcc-toolchain.cmake)

# Basic Project Configuration
project(sparrow_application_framework
    VERSION 1.0.0
    LANGUAGES C ASM
)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)  # required for inline asm

# Define executible name
set(EXECUTABLE sparrow)

# Create variables to alias path names
set(NOTE_C ${CMAKE_CURRENT_LIST_DIR}/note-c)
set(SPARROW_LORA ${CMAKE_CURRENT_LIST_DIR}/sparrow-lora)
set(SPARROW_APP_DIR ${CMAKE_CURRENT_LIST_DIR}/sparrow-application)

# Set global compile settings
add_compile_definitions(
    CURRENT_BOARD=2  # Sparrow v1.1
    STM32WLE5xx
    CORE_CM4
)
add_compile_options(
    -mcpu=cortex-m4
    -std=gnu11
    -g3
    -c
    -Og
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Wpedantic
    -Wimplicit-fallthrough=2
    -Wno-unused-parameter
    -fstack-usage
    -MMD
    -MP
    --specs=nano.specs
    -mfloat-abi=soft
    -mthumb
)

# Compile `note-c` seperately
add_library(note-c STATIC
    ${NOTE_C}/n_atof.c
    ${NOTE_C}/n_b64.c
    ${NOTE_C}/n_cjson.c
    ${NOTE_C}/n_cjson_helpers.c
    ${NOTE_C}/n_const.c
    ${NOTE_C}/n_ftoa.c
    ${NOTE_C}/n_helpers.c
    ${NOTE_C}/n_hooks.c
    ${NOTE_C}/n_i2c.c
    ${NOTE_C}/n_md5.c
    ${NOTE_C}/n_request.c
    ${NOTE_C}/n_serial.c
    ${NOTE_C}/n_str.c
    ${NOTE_C}/n_ua.c
)

target_include_directories(note-c
    PUBLIC ${NOTE_C}
)

# Compile ST platform seperately
add_library(st OBJECT
    ${SPARROW_LORA}/Application/Core/Src/system_stm32wlxx.c
    ${SPARROW_LORA}/Application/STM32CubeIDE/Application/Startup/startup_stm32wle5xx.s
    ${SPARROW_LORA}/Application/STM32CubeIDE/sys/syscalls.c
    ${SPARROW_LORA}/Application/STM32CubeIDE/sys/sysmem.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_adc.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_adc_ex.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_cortex.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_cryp.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_dma.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_dma_ex.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_exti.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_flash.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_flash_ex.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_gpio.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_i2c.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_i2c_ex.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_pwr.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_pwr_ex.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rcc.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rcc_ex.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rng.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rtc.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_rtc_ex.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_spi.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_subghz.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_tim.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_tim_ex.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_uart.c
    ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Src/stm32wlxx_hal_uart_ex.c
    ${SPARROW_LORA}/Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver/radio.c
    ${SPARROW_LORA}/Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver/radio_driver.c
    ${SPARROW_LORA}/Utilities/lpm/tiny_lpm/stm32_lpm.c
    ${SPARROW_LORA}/Utilities/misc/stm32_mem.c
    ${SPARROW_LORA}/Utilities/misc/stm32_systime.c
    ${SPARROW_LORA}/Utilities/misc/stm32_tiny_vsnprintf.c
    ${SPARROW_LORA}/Utilities/sequencer/stm32_seq.c
    ${SPARROW_LORA}/Utilities/timer/stm32_timer.c
    ${SPARROW_LORA}/Utilities/trace/adv_trace/stm32_adv_trace.c
)

target_include_directories(st SYSTEM
    PRIVATE ${SPARROW_LORA}/Application
    PRIVATE ${SPARROW_LORA}/Application/Core/Inc
    PRIVATE ${SPARROW_LORA}/Application/Core/Radio
    PRIVATE ${SPARROW_LORA}/Drivers/CMSIS/Core/Include
    PUBLIC ${SPARROW_LORA}/Drivers/CMSIS/Device/ST/STM32WLxx/Include
    PRIVATE ${SPARROW_LORA}/Drivers/CMSIS/DSP/Include
    PUBLIC ${SPARROW_LORA}/Drivers/CMSIS/Include
    PUBLIC ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Inc
    PRIVATE ${SPARROW_LORA}/Drivers/STM32WLxx_HAL_Driver/Inc/Legacy
    PUBLIC ${SPARROW_LORA}/Middlewares/Third_Party/SubGHz_Phy
    PRIVATE ${SPARROW_LORA}/Middlewares/Third_Party/SubGHz_Phy/Conf
    PRIVATE ${SPARROW_LORA}/Middlewares/Third_Party/SubGHz_Phy/stm32_radio_driver
    PUBLIC ${SPARROW_LORA}/Utilities/lpm/tiny_lpm
    PUBLIC ${SPARROW_LORA}/Utilities/misc
    PUBLIC ${SPARROW_LORA}/Utilities/sequencer
    PUBLIC ${SPARROW_LORA}/Utilities/timer
    PUBLIC ${SPARROW_LORA}/Utilities/trace/adv_trace
)

target_compile_options(st
    PRIVATE -Wno-extra
    PRIVATE -Wno-pedantic
    PRIVATE -Wno-implicit-fallthrough
)

# Compile Sparrow Runtime
add_library(sparrow-runtime OBJECT
    ${SPARROW_LORA}/Application/Core/Radio/radio_board_if.c
    ${SPARROW_LORA}/Application/Core/Src/debug.c
    ${SPARROW_LORA}/Application/Core/Src/stm32_lpm_if.c
    ${SPARROW_LORA}/Application/Core/Src/stm32wlxx_hal_msp.c
    ${SPARROW_LORA}/Application/Core/Src/stm32wlxx_it.c
    ${SPARROW_LORA}/Application/Core/Src/timer_if.c
    ${SPARROW_LORA}/Application/Core/Src/usart_if.c
    ${SPARROW_LORA}/Application/Core/Src/util_if.c
    ${SPARROW_LORA}/Application/Framework/app.c
    ${SPARROW_LORA}/Application/Framework/appinit.c
    ${SPARROW_LORA}/Application/Framework/atp.c
    ${SPARROW_LORA}/Application/Framework/dfu.c
    ${SPARROW_LORA}/Application/Framework/dfuload.c
    ${SPARROW_LORA}/Application/Framework/flash.c
    ${SPARROW_LORA}/Application/Framework/gateway.c
    ${SPARROW_LORA}/Application/Framework/led.c
    ${SPARROW_LORA}/Application/Framework/note.c
    ${SPARROW_LORA}/Application/Framework/post.c
    ${SPARROW_LORA}/Application/Framework/radioinit.c
    ${SPARROW_LORA}/Application/Framework/sched.c
    ${SPARROW_LORA}/Application/Framework/sensor.c
    ${SPARROW_LORA}/Application/Framework/trace.c
    ${SPARROW_LORA}/Application/Framework/util.c
    ${SPARROW_LORA}/Application/Gateway/auth.c
)

target_include_directories(sparrow-runtime
    PUBLIC ${SPARROW_LORA}/Application
    PUBLIC ${SPARROW_LORA}/Application/Core/Inc
    PUBLIC ${SPARROW_LORA}/Application/Framework
)

add_dependencies(sparrow-runtime
    note-c
    st
)

target_link_libraries(sparrow-runtime
    PUBLIC note-c
    PUBLIC st
)

if (SPARROW_LORA_ONLY)

# Include core applications in compilation
target_sources(sparrow-runtime
    PRIVATE ${SPARROW_LORA}/Application/Sensor/bme280/bme280.c
    PRIVATE ${SPARROW_LORA}/Application/Sensor/bme.c
    PRIVATE ${SPARROW_LORA}/Application/Sensor/button.c
    PRIVATE ${SPARROW_LORA}/Application/Sensor/init.c
    PRIVATE ${SPARROW_LORA}/Application/Sensor/ping.c
    PRIVATE ${SPARROW_LORA}/Application/Sensor/pir.c
)

target_include_directories(sparrow-runtime
    PUBLIC ${SPARROW_LORA}/Application/Sensor
    PUBLIC ${SPARROW_LORA}/Application/Sensor/bme280
)

else() # NOT SPARROW_LORA_ONLY

# Collect Sparrow Applications
if(IS_DIRECTORY ${SPARROW_APP_DIR})
    if(EXISTS ${SPARROW_APP_DIR}/CMakeLists.txt)
        add_subdirectory(${SPARROW_APP_DIR})
        if (NOT TARGET sparrow-application)
            message(WARNING "Could not find the sparrow-application target. No user-defined sensors will be imported")
        endif()
    else()
        message(WARNING "Could not find CMakeList.txt. No user-defined sensors will be imported.")
    endif()
endif()

endif() # SPARROW_LORA_ONLY

# Generate .elf .bin .hex .map and .list files
add_executable(${EXECUTABLE}
    ${SPARROW_LORA}/Application/Core/Src/main.c
)

add_dependencies(${EXECUTABLE}
    sparrow-runtime
    st
)

target_link_libraries(${EXECUTABLE}
    PRIVATE sparrow-runtime
    PRIVATE st
)

if (TARGET sparrow-application)
    target_sources(${EXECUTABLE}
        PRIVATE ${SPARROW_APP_DIR}/init.c
    )

    add_dependencies(${EXECUTABLE}
        sparrow-application
    )

    target_link_libraries(${EXECUTABLE}
        PRIVATE sparrow-application
    )
endif()

target_link_options(${EXECUTABLE}
    PRIVATE -mcpu=cortex-m4
    PRIVATE -T${SPARROW_LORA}/Application/STM32CubeIDE/STM32WLE5xx_FLASH.ld
    PRIVATE --specs=nosys.specs
    PRIVATE -Wl,-Map=${EXECUTABLE}.map
    PRIVATE -Wl,--gc-sections
    PRIVATE -static
    PRIVATE --specs=nano.specs
    PRIVATE -mfloat-abi=soft
    PRIVATE -mthumb
    PRIVATE -Wl,--start-group -lc -lm -Wl,--end-group
    PRIVATE -Wl,--print-memory-usage
)

# Additional ARM processing
add_custom_command(TARGET ${EXECUTABLE}
    POST_BUILD
    # Rename executable
    COMMAND mv ${EXECUTABLE} ${EXECUTABLE}.elf

    # Print executable size
    COMMAND ${CMAKE_SIZE_UTIL} ${EXECUTABLE}.elf

    # Create list file
    COMMAND ${CMAKE_OBJDUMP} -hS ${EXECUTABLE}.elf > ${EXECUTABLE}.list

    # Create hex file
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${EXECUTABLE}.elf ${EXECUTABLE}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${EXECUTABLE}.elf ${EXECUTABLE}.bin
)
