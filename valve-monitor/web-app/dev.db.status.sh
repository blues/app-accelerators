#!/usr/bin/env bash

# Bash strict
set -euo pipefail

readonly SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

source $SCRIPT_DIR/dev.env.sh

docker ps | grep -E $POSTGRES_CONTAINER ||
    echo "$POSTGRES_CONTAINER is NOT running." && exit 1
docker ps | head -n1
