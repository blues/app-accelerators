#!/usr/bin/env bash

# Bash strict
set -euo pipefail

readonly SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd "$SCRIPT_DIR" # cd to this script's dir

echo This script will clear your database and reinitialize it.
read -p "Continue (y/n)? " choice
case "$choice" in 
  y|Y ) echo "yes";;
  n|N ) exit 0;;
  * ) exit 10;;
esac

set -o allexport
source .env
set +o allexport

#### Reset the Datastore and generate the datastore tables
yarn db:reset
#### Seed the datastore
yarn db:init
#### Update the datastore schema
yarn prisma db push

echo Database has been reinitialized.
