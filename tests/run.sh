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
  run_test test_delete
  run_test test_tls
  run_test test_not_found
  run_test test_invalid_method
  run_test test_invalid_port
  run_test test_big_file

  echo OK
}

function run_test
{
  echo "---------------- $* ----------------"
  "$@"
}

function test_basic
{
  local readonly port=18111
  $BIN/evanescent.exe --port $port &
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

function test_delete
{
  local readonly port=18111
  $BIN/evanescent.exe --port $port &
  local readonly pid=$!
  local readonly host="127.0.0.1:$port"

  sleep 0.01

  # push data (HTTP-PUT) to URL
  curl -X PUT http://$host/DeleteMe \
    -d "@$scriptDir/expected.txt"

  # delete it
  curl -X DELETE http://$host/DeleteMe

  # get data back (HTTP-GET) from URL: should fail
  if curl --fail --silent -X GET http://$host/DeleteMe >/dev/null ; then
    echo "Resource was not deleted!" >&2
    return 1
  fi

  kill -INT $pid
  wait $pid
}

function test_big_file
{
  local readonly port=18111
  $BIN/evanescent.exe --port $port &
  local readonly pid=$!
  local readonly host="127.0.0.1:$port"

  # Generate big file (approx 6Mb)
  seq 1000000 > $tmpDir/big_file_ref.txt

  sleep 0.01

  curl \
    --silent \
    -H "Transfer-Encoding: chunked" \
    -H "Expect: 100-continue" \
    -X PUT \
    -d "@$scriptDir/big_file_ref.txt" \
    http://$host/ThisIsABigFile

  curl \
    --silent \
    -X GET \
    http://$host/ThisIsABigFile > $tmpDir/big_file_new.txt

  kill -INT $pid
  wait $pid

  compare $scriptDir/big_file_ref.txt $tmpDir/big_file_new.txt
}

function test_not_found
{
  local readonly port=15222
  $BIN/evanescent.exe --port $port >/dev/null &
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
  $BIN/evanescent.exe --port $port >/dev/null &
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

function test_tls
{
  $BIN/evanescent.exe --tls &
  local readonly pid=$!
  local readonly host="127.0.0.1:9000"

  sleep 0.1
  curl --Silent -k -X PUT --data-binary "@$scriptDir/expected.txt" https://$host/yo.dat
  curl --Silent -k https://$host/yo.dat > $tmpDir/result.txt

  # generate medium file, and push it
  seq 100000 > $tmpDir/medium_file.txt
  curl --Silent --fail --insecure -X PUT --data-binary "@$tmpDir/medium_file.txt" https://$host/medium.dat
  curl --Silent --fail --insecure -X GET https://$host/medium.dat >$tmpDir/medium_file_get.txt

  if ! diff $tmpDir/medium_file.txt $tmpDir/medium_file_get.txt ; then
    echo "Truncated file" >&2
    exit 1
  fi

  kill -INT $pid
  wait $pid

  compare $scriptDir/expected.txt $tmpDir/result.txt
}

function compare
{
  local ref=$1
  local new=$2

  # cp "$new" "$ref"
  diff -Naur "$ref" "$new"
}

main
