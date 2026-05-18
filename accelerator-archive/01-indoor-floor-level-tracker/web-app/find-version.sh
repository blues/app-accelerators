#!/usr/bin/env bash
echo "ver" \
    "$(date --rfc-3339=seconds)" \
    "$(git describe --exclude='*' --always --dirty=-delta)" \

