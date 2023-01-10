#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
source $SCRIPT_DIR/dev.env.sh

# docker container with database stored on host OS filesystem so it persists
docker volume rm $DOCKER_DATABASE_VOLUME

echo Persistent database deleted.
