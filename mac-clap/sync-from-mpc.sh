#!/bin/sh
# Bring this desktop build's copy of the plug-in up to the MPC version in ../src, then build and test it.
#
# src/ here is the MPC plug-in's sources as they are, plus this directory's own edits (desktop-fork.patch: the page's
# K1-K3 footer and legend removed, 9-row lists with pinned tops, "click or drag" text, the tiles extension). They go
# stale whenever the MPC sources change: this script copies them over and re-applies the edits, and stops if a hunk
# no longer applies instead of leaving a half-updated copy behind.
set -eu

ROOT=$(cd "$(dirname "$0")" && pwd)

cp "$ROOT"/../src/*.h "$ROOT"/../src/*.cpp "$ROOT"/../src/exports.map "$ROOT/src/"
cp "$ROOT"/../src/ui/*.h "$ROOT/src/ui/"
cp "$ROOT/../src/melodee_screen.h" "$ROOT/desktop/melodee_screen.h"

echo "Applying this build's edits (desktop-fork.patch)..."
patch -p1 -d "$ROOT" < "$ROOT/desktop-fork.patch"

echo "Version: $(sed -n 's/^#define CORTEX_VERSION "\(.*\)"/\1/p' "$ROOT/src/params.h")"
exec "$ROOT/build-macos.sh"
