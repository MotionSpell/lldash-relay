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

  # push data (HTTP-PUT) to URL1
  curl \
    -H "User-Agent: Robocop" \
    -X PUT -d "Murphy" \
    http://127.0.0.1:9000/OmniConsumerProducts

  # push data (HTTP-PUT) to URL2
  curl \
    -H "User-Agent: Robocop" \
    -X PUT -d "DickJones" \
    http://127.0.0.1:9000/OmniConsumerProducts/enemies

  # get data back (HTTP-GET) from URL1
  curl \
    --silent \
    -H "User-Agent: Robocop" \
    -X GET \
    http://127.0.0.1:9000/OmniConsumerProducts > $tmpDir/result.txt

  # get data back (HTTP-GET) from URL2
  curl \
    --silent \
    -H "User-Agent: Robocop" \
    -X GET \
    http://127.0.0.1:9000/OmniConsumerProducts/enemies > $tmpDir/result2.txt

  kill -INT $pid
  wait $pid

  compare $scriptDir/expected2.txt $tmpDir/result2.txt

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
