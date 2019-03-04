#!/usr/bin/env bash
set -euo pipefail

readonly BIN=$1

function main
{
  $BIN/evanescent.exe &
  local readonly pid=$!

  sleep 0.01
  curl \
    -H "User-Agent: Robocop" \
    -X PUT -d "Murphy" \
    http://127.0.0.1:9000/OmniConsumerProducts

  kill $pid

  wait
  echo OK
}

main
