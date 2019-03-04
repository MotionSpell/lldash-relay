#!/usr/bin/env bash
# Helper script to generate a coverage report for the full test suite.
# Usage: ./scripts/cov.sh
# The result is at: bin-cov/html/index.html
set -euo pipefail

# Build instrumented version
readonly BIN=bin-cov
rm -rf $BIN
trap "rm -rf $BIN" EXIT # free disk space

export BIN
export CXXFLAGS="--coverage -fno-inline -fno-elide-constructors"
export LDFLAGS="--coverage"
make -j`nproc`

tests/run.sh $BIN
find $BIN

# Generate coverage report
lcov --capture -d $BIN -o $BIN/profile-full.txt
lcov --remove $BIN/profile-full.txt '/usr/include/*' '/usr/lib/*' -o $BIN/profile.txt
genhtml -o cov-html $BIN/profile.txt

echo "Coverage report is available in cov-html/index.html"
