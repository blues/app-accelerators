#!/usr/bin/env bash

# Bash strict
set -euo pipefail

readonly SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd "$SCRIPT_DIR" # cd to this script's dir

set -o allexport
source .env
set +o allexport

echo Starting tunnel. Use ctrl+c to stop it.

npx localtunnel --port 4000 --subdomain "$SITE_SUBDOMAIN"
