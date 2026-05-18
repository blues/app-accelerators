#
# This CMake setup exists for the sole purpose of testing
# notecard_env_var_manager. It is not intended to be used via include,
# add_subdirectory, etc. in another project.
#

cmake_minimum_required(VERSION 3.13)

if ("${CMAKE_BINARY_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed.
    Please create a build directory and use `cmake ..` inside it.
    NOTE: cmake will now create CMakeCache.txt and CMakeFiles/*.
          You must delete them, or cmake will refuse to work.")
endif()

project(notecard_env_var_manager_tests)

# Automatically ignore CMake build directory.
if(NOT EXISTS ${PROJECT_BINARY_DIR}/.gitignore)
    file(WRITE ${PROJECT_BINARY_DIR}/.gitignore "*")
endif()

# Catch2 v3 requires C++14.
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(BUILD_SHARED_LIBS True)

option(NEVM_COVERAGE "Compile for test coverage reporting." OFF)
option(NEVM_MEM_CHECK "Run tests with Valgrind." OFF)
option(NEVM_BUILD_CATCH "Fetch and build Catch2 from source." OFF)

include(FetchContent)

FetchContent_Declare(
    note_c
    GIT_REPOSITORY https://github.com/blues/note-c.git
    GIT_TAG        master
    # Download note-c into a directory called note-c under the _deps directory.
    # This is required for the note-c includes in the manager code to resolve
    # correctly.
    SOURCE_DIR     ${FETCHCONTENT_BASE_DIR}/note-c
)
FetchContent_MakeAvailable(note_c)

set(NEVM_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

add_library(
    notecard_env_var_manager SHARED
    ${NEVM_SRC_DIR}/NotecardEnvVarManager.c
)
target_compile_options(
    notecard_env_var_manager
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
)
target_compile_definitions(
    notecard_env_var_manager
    PUBLIC
        NEVM_TEST
)
target_include_directories(
    notecard_env_var_manager
    PUBLIC
        ${NEVM_SRC_DIR}
        # This allows us to include note-c headers (e.g. note-c/note.h).
        ${FETCHCONTENT_BASE_DIR}
)
target_link_libraries(
    notecard_env_var_manager
    PUBLIC
        note_c
)

if(NEVM_MEM_CHECK)
    # Go ahead and make sure we can find valgrind while we're here.
    find_program(VALGRIND valgrind REQUIRED)
    message(STATUS "Found valgrind: ${VALGRIND}")
    set(MEMORYCHECK_COMMAND_OPTIONS "--leak-check=full --error-exitcode=1")
endif(NEVM_MEM_CHECK)
include(CTest)

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

if(NEVM_BUILD_CATCH)
    download_catch2()
else()
    find_package(Catch2 3)

    if(Catch2_FOUND)
        message(STATUS "Found Catch2: ${Catch2_CONFIG}")
    else()
        message(FATAL_ERROR "Couldn't find installed Catch2. To download Catch2 and build from source, re-run cmake with -DNEVM_BUILD_CATCH=1.")
    endif(Catch2_FOUND)
endif(NEVM_BUILD_CATCH)

include(Catch)

set(NEVM_TEST_TARGETS "")
set(NEVM_TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/test)

macro(add_test TEST_NAME)
    add_executable(
        ${TEST_NAME}
        ${NEVM_TEST_DIR}/src/${TEST_NAME}.cpp
    )
    target_include_directories(
        ${TEST_NAME}
        PRIVATE
            ${NEVM_TEST_DIR}/include
        # PRIVATE ${NOTE_C_SRC_DIR}
    )
    target_link_libraries(
        ${TEST_NAME}
        PRIVATE
            notecard_env_var_manager
            Catch2::Catch2WithMain
    )

    list(APPEND NEVM_TEST_TARGETS ${TEST_NAME})

    catch_discover_tests(${TEST_NAME})
endmacro(add_test)

add_test(NotecardEnvVarManager_test)

if(NEVM_COVERAGE)
    find_program(LCOV lcov REQUIRED)
    message(STATUS "Found lcov: ${LCOV}")

    target_compile_options(
        notecard_env_var_manager
        PUBLIC --coverage -g -O0
    )
    target_link_libraries(
        notecard_env_var_manager
        PUBLIC gcov
    )

    # Create the coverage directory and run ctest.
    add_custom_target(
        coverage
        COMMAND mkdir -p coverage
        COMMAND ${CMAKE_CTEST_COMMAND}
        WORKING_DIRECTORY ${CMAKE_CURENT_BINARY_DIR}
    )
    # We don't care about coverage for the tests themselves.
    string(APPEND LCOV_EXCLUDE "--exclude '*/test/*' ")
    # Nor do we care about note-c.
    string(APPEND LCOV_EXCLUDE "--exclude '*/note-c/*' ")
    separate_arguments(LCOV_EXCLUDE_LIST NATIVE_COMMAND "${LCOV_EXCLUDE}")
    # Run lcov to produce a coverage report in the coverage directory.
    add_custom_command(
        TARGET coverage POST_BUILD
        COMMAND lcov --capture --no-external --directory ${CMAKE_CURRENT_LIST_DIR} ${LCOV_EXCLUDE_LIST} --rc lcov_branch_coverage=1 --output-file lcov.info
        COMMAND lcov --summary --rc lcov_branch_coverage=1 lcov.info
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/coverage
    )
    # The tests have to be built before we can generate the coverage report.
    add_dependencies(coverage ${NEVM_TEST_TARGETS})
    # Delete the coverage directory when make clean is run.
    set_property(
        DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
        ${CMAKE_CURRENT_BINARY_DIR}/coverage
    )
endif(NEVM_COVERAGE)
