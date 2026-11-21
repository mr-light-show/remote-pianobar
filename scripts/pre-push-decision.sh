#!/usr/bin/env bash
# Decide what .githooks/pre-push must run. Reads the hook's stdin (one line
# per pushed ref: "<local ref> <local sha> <remote ref> <remote sha>") and
# prints one of:
#
#   docs   only docs changed: run nothing
#   web    only webui/ (and docs) changed: run the web unit tests
#   stamp  make gates already passed for this exact commit on a clean tree
#   full   run the full hook tests
#
# The scope (scripts/changed-scope.sh) and the stamp both describe HEAD, so
# they are trusted only when every pushed ref is HEAD itself. Pushing any
# other commit (another branch, a tag object, src:dst) is always full.
# Deleted refs (local sha all zeros) need no tests.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
ZERO=0000000000000000000000000000000000000000
head_sha="$(git rev-parse HEAD)"

while read -r _localRef localSha _remoteRef _remoteSha; do
	[ -n "${localSha:-}" ] || continue
	[ "$localSha" = "$ZERO" ] && continue
	if [ "$localSha" != "$head_sha" ]; then
		echo full
		exit 0
	fi
done

scope="$("$DIR/changed-scope.sh")"
if [ "$scope" != full ]; then
	echo "$scope"
	exit 0
fi

stamp="build/gates/passed"
if [ -z "$(git status --porcelain -uall)" ] && [ -f "$stamp" ] &&
		[ "$(cat "$stamp")" = "$head_sha" ]; then
	echo stamp
	exit 0
fi
echo full
