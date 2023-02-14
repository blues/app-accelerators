# Copyright 2023 Blues Inc.  All rights reserved.
# Use of this source code is governed by licenses granted by the
# copyright holder including that found in the LICENSE file.

# Target name `sparrow-application` is required for import
add_library(sparrow-application STATIC
    ${CMAKE_CURRENT_LIST_DIR}/rsm/rsm.c
)

target_include_directories(sparrow-application
    PUBLIC ${CMAKE_CURRENT_LIST_DIR}/rsm
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
