#!/bin/bash -v
set -x

# Usage info
show_help() {
cat << EOF
Usage: ${0##*/} [-h] [-p PROJECTID] [-f FLEETID] [-t TOKEN] [-i INTERVAL] [-l VALUES]
Set Fleet-Level Environment Variables Using the Notehub API.

     -h            display this help and exit
     -p PROJECTID  Notehub Project ID
     -f FLEETID    Nothub Fleet ID
     -t TOKEN      Notehub API Token
     -i INTERVAL   Environment Variable for display_interval_sec
     -v VALUES     Environment Variable for display_values
EOF
}

# Update Environment Variables for a Fleet
# Usage ./update-fleet.sh -p app:123 -f fleet:456 -t abcdefx -i 30 -v "Foo!Bar!Baz!"

product=""
fleet=""
token=""
interval=0
values=""
interval_obj='{}'
values_obj='{}'

OPTIND=1

while getopts "h:p:f:t:i:v:" opt; do
    case $opt in
        h)
            show_help
            exit 0
            ;;
        p)
            product=$OPTARG
            ;;
        f)
            fleet=$OPTARG
            ;;
        t)
            token=$OPTARG
            ;;
        i)
            interval=$OPTARG
            ;;
        v)
            values=$OPTARG
            ;;
        *)
            show_help >&2
            exit 1
            ;;
    esac
done
shift "$((OPTIND-1))"

if [[ $interval -gt 0 ]]
then
  interval_obj=$(jq --null-input \
  --arg display_interval_sec "$interval" \
  '{"display_interval_sec": $display_interval_sec}')
fi

if [[ -n "$values" ]]
then
  values_obj=$(jq --null-input \
  --arg display_values "$values" \
  '{"display_values": $display_values}')
fi

env_vars=$(echo "$interval_obj" "$values_obj" | jq -s 'add')

curl --request PUT \
  --url "https://api.notefile.net/v1/projects/$product/fleets/$fleet/environment_variables" \
  --header "Content-Type: application/json" \
  --header "X-SESSION-TOKEN: $token" \
  --data "{\"environment_variables\":$env_vars}"
