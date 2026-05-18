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
        message(FATAL_ERROR "Couldn't find installed Catch2. To download Catch2 and build from source, re-run cmake with -DBUILD_CATCH=1.")
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
add_test(NoteTime_test)
add_test(NoteRegion_test)
add_test(NoteLocationValid_test)
add_test(NoteIsConnected_test)
add_test(NoteGetNetStatus_test)
add_test(NoteGetVersion_test)
add_test(NoteGetLocation_test)
add_test(NoteSetLocation_test)
add_test(NoteClearLocation_test)
add_test(NoteGetLocationMode_test)
add_test(NoteSetLocationMode_test)
add_test(NoteGetServiceConfig_test)
add_test(NoteGetStatus_test)
add_test(NoteSleep_test)
add_test(NoteNewRequest_test)
add_test(NoteNewCommand_test)
add_test(NoteErrorClean_test)
add_test(NoteSetFnMutex_test)
add_test(NoteSetFnI2CMutex_test)
add_test(NoteSetFnNoteMutex_test)
add_test(NoteSetFnSerial_test)
add_test(NoteSetFnI2C_test)
add_test(JIsPresent_test)
add_test(JGetString_test)
add_test(JGetObject_test)
add_test(JBoolValue_test)
add_test(JStringValue_test)
add_test(JNumberValue_test)
add_test(JGetNumber_test)
add_test(JIntValue_test)
add_test(JGetInt_test)
add_test(JGetBool_test)
add_test(JIsNullString_test)
add_test(JIsExactString_test)
add_test(JContainsString_test)
add_test(JAddBinaryToObject_test)
add_test(JGetBinaryFromObject_test)
add_test(JItoA_test)
add_test(JAtoI_test)
add_test(JGetItemName_test)
add_test(JAllocString_test)
add_test(JType_test)
add_test(JGetType_test)
add_test(NotePrintf_test)
add_test(NoteDebugf_test)
add_test(NoteSetEnvDefault_test)
add_test(NoteSetEnvDefaultNumber_test)
add_test(NoteGetEnvNumber_test)
add_test(NotePayloadSaveAndSleep_test)
add_test(NoteWake_test)
add_test(NotePayloadRetrieveAfterSleep_test)
add_test(NoteUserAgent_test)
add_test(NoteFactoryReset_test)
add_test(NoteSetProductID_test)
add_test(NoteSetSerialNumber_test)
add_test(NoteSetUploadMode_test)
add_test(NoteSetSyncMode_test)
add_test(NoteTemplate_test)
add_test(NoteTimeSet_test)
add_test(NoteLocalTimeST_test)
add_test(NoteAdd_test)
add_test(NoteSendToRoute_test)
add_test(NoteGetVoltage_test)
add_test(NoteGetTemperature_test)
add_test(NoteGetContact_test)
add_test(NoteSetContact_test)
add_test(NoteDebugSyncStatus_test)

if(NOTE_C_COVERAGE)
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
    # We don't care about coverage for the tests themselves.
    string(APPEND LCOV_EXCLUDE "--exclude '*/test/*' ")
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
endif(NOTE_C_COVERAGE)
