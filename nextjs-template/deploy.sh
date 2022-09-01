#! /usr/bin/env bash
# bash boilerplate
set -euo pipefail # strict mode
readonly SCRIPT_NAME="$(basename "$0")"
readonly SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
function log { # Log a message to the terminal.
  echo -e "[$SCRIPT_NAME]" "${@:-}"
}

# Config
readonly COMPOSE_FILE=docker-compose.azure.yml
# derived
readonly LOCAL_COMPOSE=(docker-compose --file "$COMPOSE_FILE") # note hypen
readonly CLOUD_COMPOSE=(docker compose --file "$COMPOSE_FILE") # no hyphen

## Low level functions
function verify_docker_context_is_azure(){
  docker context use "$AZURE_DOCKER_CONTEXT" || {
    log "Failed: docker context use $AZURE_DOCKER_CONTEXT"
    return 11
  }
  docker context inspect | grep "Type" | grep "aci" || {
    log 'Error: `docker context inspect` does not report "Type":"aci".'
    log 'FYI, [docker context ls] shows:'
    docker context ls
    return 11
  }
}

function verify_docker_is_installed(){
  docker version | head -1
}

function verify_compose_cli_is_installed(){
  docker version | grep 'Cloud integration'
}

function verify_prereqs_are_met(){
  verify_docker_is_installed || {
    log 'Error: Docker is not found. To install it, see https://docs.docker.com/get-docker/'
    return 11
  }
  verify_compose_cli_is_installed || {
    log 'Error: Docker is not found. To install it, see https://docs.docker.com/get-docker/'
    return 11
  }
  verify_docker_context_is_azure || {
    log 'Error: Docker context in use is not Azure.'
    log 'Please complete the following tasks. Guidance is linked below.'
    log '- Install Docker'
    log '- Install "compose-cli", a special cloud version of docker-compose.'
    log '- Activate the context with: $ docker context use NAME'
    log 'Guidance through "Run a container" here: https://docs.docker.com/cloud/aci-integration/'
    return 11
  }
}

function build_docker_images() {
  docker context use default
  "${LOCAL_COMPOSE[@]}" build
}

function push_docker_images() {
  docker context use default
  "${LOCAL_COMPOSE[@]}" push
}

function prepare_to_deploy(){
  build_docker_images || {
    log 'Error: Could not build docker image(s)'
    log '       Are you missing files in the .dockerignore whitelist?'
    return 11
  }
  push_docker_images || {
    log 'Error: Could not push docker image(s) to registry. Do you need to `docker login --username NAME`?'
    return 11
  }
}

function deploy(){
  docker context use "$AZURE_DOCKER_CONTEXT"
  "${CLOUD_COMPOSE[@]}" up || {
    log 'Error: Docker Compose CLI failed'
    return 11
  }
}

function source_env_file() {
  local retcode=0
  log "Sourcing environment from $1"
  set -o allexport
  set +o errexit
  source "$1" 
  retcode=$?
  set -o errexit
  set +o allexport
  return $retcode
}

function load_env_file() {
  source_env_file "$1" || log "Info: Coud not load $1 environment file."
}

function add_date_to_docker_image_env() {
    export APP_SITE_DOCKER_TAG="${APP_SITE_DOCKER_IMAGE}:$(date +%Y-%m-%d-%H%M)"
}

function configure_environment() {
  load_env_file '.env'
  load_env_file '.env.local'
  load_env_file '.env.production.local'
  add_date_to_docker_image_env
}

function main() {
  cd "$SCRIPT_DIR"

  log "Reference Web App to the Cloud."

  configure_environment || {
    log "Error: Could not configure environment."
    return 11
  }
  verify_prereqs_are_met || {
    log "Error: Prerequisites are not met."
    return 11
  }
  prepare_to_deploy || {
    log "Error: Could not prepare to deploy."
    return 11
  }
  deploy || {
    log "Error: Could not deploy."
    return 11
  }

  log '🚀 Successful deployment.'
  log '🔃 To deploy new changes, simply run this script again.'
  log '🚮 To delete the deployment or see cloud details, visit the Azure Portal:'\
      'https://portal.azure.com/#blade/HubsExtension/BrowseResource/resourceType/Microsoft.ContainerInstance%2FcontainerGroups'
  log '⏰ In a few minutes the site should be visible here:'
  log "🔜 https://$SITE_DNS"
}

main
