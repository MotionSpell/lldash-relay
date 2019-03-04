#!/usr/bin/env bash
set -euo pipefail

readonly BIN=$1

readonly tmpDir=/tmp/test-evanescent-$$
trap "rm -rf $tmpDir" EXIT
mkdir -p $tmpDir

readonly scriptDir=$(dirname $0)

function main
{
  $BIN/evanescent.exe &
  local readonly pid=$!

  sleep 0.01

  # push data (HTTP-PUT)
  curl \
    -H "User-Agent: Robocop" \
    -X PUT -d "Murphy" \
    http://127.0.0.1:9000/OmniConsumerProducts

  # get data back (HTTP-GET)
  curl \
    --silent \
    -H "User-Agent: Robocop" \
    -X GET \
    http://127.0.0.1:9000/OmniConsumerProducts > $tmpDir/result.txt

  kill -INT $pid
  wait $pid

  compare $scriptDir/expected.txt $tmpDir/result.txt

  echo OK
}

function compare
{
  local ref=$1
  local new=$2

  # cp "$new" "$ref"
  diff -Naur "$ref" "$new"
}

main
