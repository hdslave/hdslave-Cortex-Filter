#!/bin/sh
# Prints how loud the presets, cubes, Emphasis and Drive are against the dry input (tools/balance.cpp).
set -eu
cd "$(dirname "$0")/.."
docker --context "${DOCKER_CONTEXT:-colima-melodee}" run --rm --platform linux/arm64 -v "$PWD:/work" -w /work melodee-builder \
    sh -c "mkdir -p build && g++ -std=c++17 -O2 -ffp-contract=fast -Isrc tools/balance.cpp -o build/balance && build/balance"
