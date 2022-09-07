# Bash strict
set -euo pipefail

if [ -z "$SCRIPT_DIR" ]; then
readonly SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
fi
set -o allexport
# ideally env var interpolation should happen after both files have been read
if [ -f $SCRIPT_DIR/.env.local ]; then
    source $SCRIPT_DIR/.env.local
fi
source $SCRIPT_DIR/.env
if [ -f $SCRIPT_DIR/.env.local ]; then
    source $SCRIPT_DIR/.env.local
fi
set +o allexport

# These vars are used by the development database scripts
DOCKER_DATABASE_VOLUME="${DOCKER_DATABASE_VOLUME:-$APP_ID.db.persistence.volume}"
POSTGRES_CONTAINER="${POSTGRES_CONTAINER:-$APP_ID-postgresql-container}"

