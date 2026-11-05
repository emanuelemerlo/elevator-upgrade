#!/bin/bash
# Starts a built program with the web interface and checks that it answers.
# Usage: smoke-test.sh <folder with the program, web/ and config.json> <program file name> [expected version]
set -euo pipefail

folder="$1"
program="$2"
expected="${3:-}"
port=18080

cd "$folder"

version=$("./$program" --version)
echo "$version"
if [ -n "$expected" ] && [ "$version" != "Elevator playground $expected" ]; then
  echo "Unexpected version: '$version' instead of '$expected'" >&2
  exit 1
fi

# stdin is not a console: the program keeps running until it is stopped.
"./$program" --web --port "$port" < /dev/null > smoke-test.log 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true; taskkill //F //IM "$program" >/dev/null 2>&1 || true' EXIT

for attempt in $(seq 1 40); do
  if curl -fsS "http://127.0.0.1:$port/api/state" -o state.json 2>/dev/null; then break; fi
  sleep 0.5
done

# Every answer is saved to a file before it is checked: with "curl | grep -q" grep stops reading at the first match,
# curl fails to write the rest of a long page and pipefail turns that into a false failure.
grep -q '"clock"' state.json || { echo "No state from the web interface"; cat smoke-test.log; exit 1; }

curl -fsS "http://127.0.0.1:$port/" -o page.html
grep -q "Elevator playground" page.html || { echo "The page is not served"; exit 1; }

curl -fsS -X POST -d "from=0&to=5" "http://127.0.0.1:$port/api/call" -o call.json
grep -q '"id"' call.json || { echo "A call was refused"; cat call.json; exit 1; }

curl -fsS -X POST -d "action=speed&value=2000" "http://127.0.0.1:$port/api/control" -o control.json

for attempt in $(seq 1 20); do
  curl -fsS "http://127.0.0.1:$port/api/state" -o state.json
  if grep -q '"completed":[1-9]' state.json; then break; fi
  sleep 0.5
done
grep -q '"completed":[1-9]' state.json || { echo "Nobody arrived"; exit 1; }

echo "Smoke test passed"
rm -f state.json page.html call.json control.json smoke-test.log
