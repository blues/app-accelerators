include(FetchContent)

# Catch2 v3 requires C++14.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(BUILD_CATCH "Fetch and build Catch2 from source." OFF)

macro(download_catch2)
    message(STATUS "Downloading Catch2...")
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG        v3.2.0
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endmacro(download_catch2)

if(BUILD_CATCH)
    download_catch2()
else()
    find_package(Catch2 3)

    if(Catch2_FOUND)
        message(STATUS "Found Catch2: ${Catch2_CONFIG}")
    else()
        message(STATUS "Couldn't find installed Catch2.")
        download_catch2()
    endif(Catch2_FOUND)
endif(BUILD_CATCH)

include(Catch)

set(TEST_TARGETS "")

macro(add_test TEST_NAME)
    add_executable(
        ${TEST_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/${TEST_NAME}.cpp
    )
    target_include_directories(${TEST_NAME}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${NOTE_C_SRC_DIR}
    )
    target_link_libraries(${TEST_NAME}
        PRIVATE note_c
        PRIVATE Catch2::Catch2WithMain
    )

    list(APPEND TEST_TARGETS ${TEST_NAME})

    catch_discover_tests(${TEST_NAME})
endmacro(add_test)

add_test(NoteTransaction_test)
add_test(NoteRequest_test)
add_test(NoteRequestResponse_test)
add_test(NoteRequestResponseJSON_test)
add_test(NoteRequestWithRetry_test)
add_test(NoteGetEnv_test)
add_test(NotePayload_test)
add_test(i2cNoteTransaction_test)
add_test(i2cNoteReset_test)
add_test(serialNoteTransaction_test)
add_test(serialNoteReset_test)
add_test(NoteSetFn_test)
add_test(NoteDebug_test)
add_test(NoteTransactionHooks_test)
add_test(NoteSerialHooks_test)
add_test(NotePrint_test)
add_test(NotePrintln_test)

if(COVERAGE)
    find_program(LCOV lcov REQUIRED)
    message(STATUS "Found lcov: ${LCOV}")

    target_compile_options(note_c PUBLIC --coverage -g -O0)
    target_link_libraries(note_c gcov)

    # Create the coverage directory and run ctest.
    add_custom_target(
        coverage
        COMMAND mkdir -p coverage
        COMMAND ${CMAKE_CTEST_COMMAND}
        WORKING_DIRECTORY ${CMAKE_CURENT_BINARY_DIR}
    )
    # These files are third party code that we aren't interested in testing
    # ourselves, so we don't care about coverage for them.
    set(
        EXCLUDE_FROM_COVERAGE
        "n_atof.c;n_b64.c;n_cjson.c;n_ftoa.c;n_md5.c;n_str.c"
    )
    foreach(EXCLUDE_FILE ${EXCLUDE_FROM_COVERAGE})
        string(APPEND LCOV_EXCLUDE "--exclude '*/${EXCLUDE_FILE}' ")
    endforeach()
    separate_arguments(LCOV_EXCLUDE_LIST NATIVE_COMMAND "${LCOV_EXCLUDE}")
    # Run lcov to produce a coverage report in the coverage directory.
    add_custom_command(
        TARGET coverage POST_BUILD
        COMMAND lcov --capture --no-external --directory ${NOTE_C_SRC_DIR} --rc lcov_branch_coverage=1 ${LCOV_EXCLUDE_LIST} --output-file lcov.info
        COMMAND lcov --summary --rc lcov_branch_coverage=1 lcov.info
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/coverage
    )
    # The tests have to be built before we can generate the coverage report.
    add_dependencies(coverage ${TEST_TARGETS})
    # Delete the coverage directory when make clean is run.
    set_property(
        DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
        ${CMAKE_CURRENT_BINARY_DIR}/coverage
    )
endif(COVERAGE)
