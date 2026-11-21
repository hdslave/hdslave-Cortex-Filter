#!/bin/sh
# The control steps' cost in sound (tools/artifacts.cpp), the plug-in's 8 frames against 1 (a step every frame).
set -eu
cd "$(dirname "$0")/.."
docker --context "${DOCKER_CONTEXT:-colima-melodee}" run --rm --platform linux/arm64 -v "$PWD:/work" -w /work melodee-builder sh -c "
    mkdir -p build
    g++ -std=c++17 -O2 -ffp-contract=fast -Isrc tools/artifacts.cpp -o build/artifacts-8
    g++ -std=c++17 -O2 -ffp-contract=fast -Isrc -DCORTEX_CONTROL_FRAMES=1 tools/artifacts.cpp -o build/artifacts-1
    echo 'energy above 3 kHz, every 8 frames (the plug-in):'; build/artifacts-8
    echo 'the same worked out every frame:'; build/artifacts-1"
