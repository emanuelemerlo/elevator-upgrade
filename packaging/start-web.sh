#!/bin/sh
# Starts the simulation with the web interface and opens it in the default browser.
cd "$(dirname "$0")" || exit 1
url="http://127.0.0.1:8080/"
(
  sleep 2
  if command -v open >/dev/null 2>&1; then open "$url"
  elif command -v xdg-open >/dev/null 2>&1; then xdg-open "$url"
  else echo "Open $url in your browser"
  fi
) &
exec ./Elevator --web
