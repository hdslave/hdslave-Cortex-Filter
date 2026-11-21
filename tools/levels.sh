#!/bin/sh
# Prints each cube's corner levels (tools/levels.cpp) from the build container.
set -eu
cd "$(dirname "$0")/.."
docker --context "${DOCKER_CONTEXT:-colima-melodee}" run --rm --platform linux/arm64 -v "$PWD:/work" -w /work melodee-builder \
    sh -c "mkdir -p build && g++ -std=c++17 -O2 -Isrc tools/levels.cpp -o build/levels && build/levels"
