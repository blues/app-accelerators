#!/bin/bash -v
set -x

# Get a base64 string of a provided image
image=$(base64 -i $1)

# notehub -product com.blues.nf4 -device dev:864475044220688 -req '{"req":"note.add","body":{"foo":"bar"}}'
notehub -product $2 -device $3 -req "{\"req\":\"note.add\",\"body\":{\"name\":\"$4\",\"content\":\"${image}\"}}"
