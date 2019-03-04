#!/usr/bin/env bash
# Helper script to run an instrumented version
# of the unit tests.
# Usage: ./scripts/sanitize.sh
set -euo pipefail
readonly scriptDir=$(dirname $(readlink -f $0))

# Build instrumented version

BIN=bin-asan
CXXFLAGS="-fsanitize=address,undefined"
LDFLAGS=-fsanitize=address,undefined

CXXFLAGS="$CXXFLAGS -g3"
LDFLAGS="$LDFLAGS -g"

export BIN CXXFLAGS LDFLAGS

make -j`nproc`

export TSAN_OPTIONS=halt_on_error=1
export ASAN_OPTIONS=halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
tests/run.sh $BIN

