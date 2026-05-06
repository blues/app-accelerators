#!/bin/bash

COVERAGE=0
MEM_CHECK=0

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --coverage) COVERAGE=1 ;;
        --mem-check) MEM_CHECK=1 ;;
        *) echo "Unknown parameter: $1"; exit 1 ;;
    esac
    shift
done

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
ROOT_SRC_DIR="$SCRIPT_DIR/.."

if [[ ! -f "$ROOT_SRC_DIR/CMakeLists.txt" ]]; then
    echo "Failed to find note-c root directory. (did the location of run_unit_tests.sh change?)"
    exit 1
fi

pushd $ROOT_SRC_DIR $@ > /dev/null

# Shared library makes the build smaller.
CMAKE_OPTIONS="-DBUILD_TESTS=1 -DBUILD_SHARED_LIBS=1"
BUILD_OPTIONS=""
CTEST_OPTIONS=""
if [[ $COVERAGE -eq 1 ]]; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DCOVERAGE=1"
    BUILD_OPTIONS="${BUILD_OPTIONS} coverage"
fi
if [[ $MEM_CHECK -eq 1 ]]; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DMEM_CHECK=1"
    CTEST_OPTIONS="${CTEST_OPTIONS} -T memcheck"

    # This fixes a problem when running valgrind in a Docker container when the
    # host machine is running Fedora. See https://stackoverflow.com/a/75293014.
    ulimit -n 1024
fi

cmake -B build/ $CMAKE_OPTIONS
if [[ $? -ne 0 ]]; then
    echo "Failed to run CMake."
    popd $@ > /dev/null
    exit 1
fi

cmake --build build/ -- $BUILD_OPTIONS -j
if [[ $? -ne 0 ]]; then
    echo "Failed to build code."
    popd $@ > /dev/null
    exit 1
fi

ctest --test-dir build/ --output-on-failure ${CTEST_OPTIONS}
if [[ $? -ne 0 ]]; then
    echo "ctest failed."
    popd $@ > /dev/null
    exit 1
fi

echo "Tests passed."
popd $@ > /dev/null
