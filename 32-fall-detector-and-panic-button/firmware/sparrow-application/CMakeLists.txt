# Copyright 2023 Blues Inc.  All rights reserved.
# Use of this source code is governed by licenses granted by the
# copyright holder including that found in the LICENSE file.

# Target name `sparrow-application` is required for import
add_library(sparrow-application STATIC
    ${CMAKE_CURRENT_LIST_DIR}/fall_detector/bma400.c
    ${CMAKE_CURRENT_LIST_DIR}/fall_detector/fall_detector.c
    ${CMAKE_CURRENT_LIST_DIR}/panic/panic.c
    )

target_include_directories(sparrow-application
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/fall_detector
        ${CMAKE_CURRENT_LIST_DIR}/panic
)

add_dependencies(sparrow-application
    sparrow-runtime
)

target_link_libraries(sparrow-application
    PRIVATE sparrow-runtime
)

target_compile_options(sparrow-application
    PRIVATE -Wunused-parameter
)
