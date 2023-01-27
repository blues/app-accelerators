#!/bin/bash

COVERAGE=0

while [[ "$#" -gt 0 ]]; do
    case $1 in
        --coverage) COVERAGE=1 ;;
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

CMAKE_OPTIONS=""
BUILD_OPTIONS=""
if [[ $COVERAGE -eq 1 ]]; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DCOVERAGE=1"
    BUILD_OPTIONS="${BUILD_OPTIONS} coverage"
fi

cmake -B build/ $CMAKE_OPTIONS
if [[ $? -ne 0 ]]; then
    echo "Failed to run CMake."
    popd $@ > /dev/null
    exit 1
fi

cmake --build build/ -- $BUILD_OPTIONS
if [[ $? -ne 0 ]]; then
    echo "Failed to build code."
    popd $@ > /dev/null
    exit 1
fi

ctest --test-dir build/ --output-on-failure
if [[ $? -ne 0 ]]; then
    echo "ctest failed."
    popd $@ > /dev/null
    exit 1
fi

echo "Tests passed."
popd $@ > /dev/null
