#!/usr/bin/env bash

# Bash strict
set -euo pipefail

readonly SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd "$SCRIPT_DIR" # cd to this script's dir
source ./dev.env.sh

#### Seed the datastore
yarn db:init
#### Update the datastore schema
yarn prisma db push --accept-data-loss
