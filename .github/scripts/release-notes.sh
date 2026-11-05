#!/bin/sh
# Prints the section of CHANGELOG.md for the given version (without its heading).
# Usage: release-notes.sh 1.0.0 [CHANGELOG.md]
# Exits with an error when the version has no section, so that a release cannot go out without notes.
version="$1"
changelog="${2:-CHANGELOG.md}"

notes=$(awk -v version="$version" '
  /^## \[/ {
    if (found) exit
    if (index($0, "## [" version "]") == 1) { found = 1; next }
  }
  found { print }
' "$changelog")

# Trim the blank lines at the start and at the end.
notes=$(printf '%s\n' "$notes" | sed -e '/./,$!d' | sed -e :a -e '/^\n*$/{$d;N;ba' -e '}')

if [ -z "$notes" ]; then
  echo "CHANGELOG.md has no section for version $version: add '## [$version] - date' with the release notes." >&2
  exit 1
fi

printf '%s\n' "$notes"
