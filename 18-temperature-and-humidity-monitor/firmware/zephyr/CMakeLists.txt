cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(temp_and_humidity_monitor)

set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(NOTE_C_DIR ${SRC_DIR}/note-c)


target_sources(
    app
    PRIVATE
        # Application sources.
        ${SRC_DIR}/alarm_publisher.c
        ${SRC_DIR}/bme280.c
        ${SRC_DIR}/env_updater.c
        ${SRC_DIR}/main.c
        ${SRC_DIR}/note_c_hooks.c
        ${SRC_DIR}/publisher.c
        # note-c sources.
        ${NOTE_C_DIR}/n_atof.c
        ${NOTE_C_DIR}/n_cjson.c
        ${NOTE_C_DIR}/n_const.c
        ${NOTE_C_DIR}/n_helpers.c
        ${NOTE_C_DIR}/n_i2c.c
        ${NOTE_C_DIR}/n_printf.c
        ${NOTE_C_DIR}/n_serial.c
        ${NOTE_C_DIR}/n_ua.c
        ${NOTE_C_DIR}/n_b64.c
        ${NOTE_C_DIR}/n_cjson_helpers.c
        ${NOTE_C_DIR}/n_ftoa.c
        ${NOTE_C_DIR}/n_hooks.c
        ${NOTE_C_DIR}/n_md5.c
        ${NOTE_C_DIR}/n_request.c
        ${NOTE_C_DIR}/n_str.c
)
target_include_directories(
    app
    PRIVATE ${NOTE_C_DIR}
)
