#!/usr/bin/env bash
set -euo pipefail

readonly BIN=$1

readonly tmpDir=/tmp/test-evanescent-$$
trap "rm -rf $tmpDir" EXIT
mkdir -p $tmpDir

readonly scriptDir=$(dirname $0)

function main
{
  run_test test_basic
  run_test test_not_found
  run_test test_invalid_method
  run_test test_invalid_port

  echo OK
}

function run_test
{
  echo "* $*"
  "$@"
}

function test_basic
{
  local readonly port=18111
  $BIN/evanescent.exe $port &
  local readonly pid=$!
  local readonly host="127.0.0.1:$port"

  sleep 0.01

  # push data (HTTP-PUT) to URL1
  curl \
    -H "User-Agent: Robocop" \
    -X PUT -d "@$scriptDir/expected.txt" \
    http://$host/OmniConsumerProducts

  # push trash data (HTTP-PUT) to URL2
  curl \
    -H "User-Agent: Robocop" \
    -X PUT -d "NowYouSeeMe!" \
    http://$host/OmniConsumerProducts/enemies

  # push data (HTTP-PUT) to URL2
  curl \
    -H "User-Agent: Robocop" \
    -H "Transfer-Encoding: chunked" \
    -X PUT -d "Dick Jones is the guy" \
    http://$host/OmniConsumerProducts/enemies

  # get data back (HTTP-GET) from URL1
  curl \
    --silent \
    -H "User-Agent: Robocop" \
    -X GET \
    http://$host/OmniConsumerProducts > $tmpDir/result.txt

  # get data back (HTTP-GET) from URL2
  curl \
    --silent \
    -H "User-Agent: Robocop" \
    -X GET \
    http://$host/OmniConsumerProducts/enemies > $tmpDir/result2.txt

  kill -INT $pid
  wait $pid

  compare $scriptDir/expected2.txt $tmpDir/result2.txt
}

function test_not_found
{
  local readonly port=15222
  $BIN/evanescent.exe $port >/dev/null &
  local readonly pid=$!
  local readonly host="127.0.0.1:$port"

  sleep 0.01

  # call non-implemented method
  exitCode=0
  curl \
    --fail \
    --silent \
    -X GET \
    http://$host/IDontExist || exitCode=$?

  kill -INT $pid
  wait $pid

  if [ ! $exitCode = 22 ] ; then
    echo "The server did not report the correct error (curl exit code: $exitCode (expected 22))" >&2
    exit 1
  fi
}

function test_invalid_method
{
  local readonly port=15333
  $BIN/evanescent.exe $port >/dev/null &
  local readonly pid=$!
  local readonly host="127.0.0.1:$port"

  sleep 0.01

  # call non-implemented method
  exitCode=0
  curl \
    --fail \
    --silent \
    -H "User-Agent: PierreRobin" \
    -X CRASH \
    http://$host/OmniConsumerProducts || exitCode=$?

  kill -INT $pid
  wait $pid

  if [ ! $exitCode = 22 ] ; then
    echo "The server did not report the correct error (curl exit code: $exitCode (expected 22))" >&2
    exit 1
  fi
}

function test_invalid_port
{
  if $BIN/evanescent.exe -1 2>/dev/null ; then
    echo "The server did not fail, although the request port is invalid" >&2
    exit 1
  fi
}

function compare
{
  local ref=$1
  local new=$2

  # cp "$new" "$ref"
  diff -Naur "$ref" "$new"
}

main
