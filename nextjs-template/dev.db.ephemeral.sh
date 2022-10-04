#!/usr/bin/env bash

# Bash strict
set -euo pipefail

readonly SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd "$SCRIPT_DIR" # cd to this script's dir
source ./dev.env.sh

# To run a docker image with a clean database
docker run --rm \
  -d `# detached` \
  --net=host \
  --name $POSTGRES_CONTAINER \
  -p $POSTGRES_PORT:5432 \
  -e POSTGRES_PASSWORD=$POSTGRES_PASSWORD \
  postgres

#### Wait for database to come up.
readonly seconds=45
echo "Waiting for database to come up. Will timeout in $seconds seconds."
readonly wait_for_port_cmd=(sh -c 'until nc -z $0 $1; do sleep 1; done')
timeout $seconds sh -c 'until nc -z $0 $1; do sleep 1; done' "$POSTGRES_HOST" "$POSTGRES_PORT" ||
  (
    echo "Err: database did not come up. Use ./dev.db.stop.sh and try again." &&
    exit 10
  )

#### Reset the Datastore and generate the datastore tables
yarn db:reset

 ./dev.db.update.sh

echo Database is now running the background. Use ./dev.db.stop.sh to stop it.
