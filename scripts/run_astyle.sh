#!/bin/bash

#
# This script will automatically format all version-controlled C and C++ files
# in the repository using the astyle tool with the One True Brace Style (OTBS).
# If a file shouldn't be formatted, it needs to be listed in the EXCLUDE_FILES
# array below with a path relative to the source code root.
#

if [[ ! $(which astyle) ]]; then
    echo "astyle binary not found. Please install it and try again."
    exit 1
fi

# These files won't be formatted.
EXCLUDE_FILES=(
    test/include/fff.h
)

if [[ `git status --porcelain --untracked=no` ]]; then
    echo "Local changes detected. Please commit, stash, or discard any working tree changes prior to running this script."
    exit 1
fi

# Run astyle on all version-controlled C and C++ files.
for FILE in $(git ls-files '*.c' '*.h' '*.cpp' '*.hpp')
do
    # Skip files listed in EXCLUDE_FILES.
    if [[ ! " ${EXCLUDE_FILES[*]} " =~ " ${FILE} " ]]; then
        astyle --suffix=none --style=otbs $FILE
    fi
done

# If there are new formatting changes after running astyle, exit with a non-zero
# value.
if [[ `git status --porcelain --untracked=no` ]]; then
    echo "New formatting changes left in working tree."
    exit 1
else
    echo "No new formatting changes."
fi
