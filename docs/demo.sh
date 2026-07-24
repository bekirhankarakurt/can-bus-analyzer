#!/usr/bin/env bash
# Scripted terminal session behind docs/demo.gif. Re-record with docs/record_demo.sh.
set -euo pipefail
cd "$(dirname "$0")/.."
export PATH="$PWD/build:$PATH"
log=/tmp/demo.log
trap 'rm -f "$log"' EXIT

run() {
  printf '\033[1;32m$\033[0m '
  local cmd="$1" i
  for ((i = 0; i < ${#cmd}; i++)); do
    printf '%s' "${cmd:i:1}"
    sleep 0.025
  done
  printf '\n'
  sleep 0.4
  eval "$cmd"
  sleep "${2:-1.5}"
}

clear
run "canscan monitor --replay tests/fixtures/sample.log --dbc tests/fixtures/sample.dbc --speed 0 -n 4 -f 100 -f 200 -f 18FEF100 2>/dev/null" 2.5
clear
run "canscan monitor --generate tests/fixtures/sample.dbc --live -t 4 2>/dev/null" 2
clear
run "canscan record --generate tests/fixtures/sample.dbc --speed 0 -n 400 -o $log 2>&1 | tail -1 && canscan stats --replay $log --speed 0" 3
