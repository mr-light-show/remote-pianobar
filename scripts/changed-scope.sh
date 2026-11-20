#!/usr/bin/env bash
# Print what a change can affect: docs, web or full.
#
# usage: scripts/changed-scope.sh [base]
#
# Looks at `git diff --name-only <base>...HEAD` plus uncommitted and
# untracked files (git status). Used by `make gates`, .githooks/pre-push and
# the CI `changes` job, so all three skip the same things.
#
#   docs  every file is *.md, docs/**, .agents/**, .cursor/** or LICENSE*
#         (also: no changes at all)
#   web   every file is under webui/** or is docs
#   full  anything else: src/, test/, locale/, Makefile, scripts/, .github/,
#         .githooks/, unknown paths, or a base that cannot be resolved
#
# base: first argument, else SCOPE_BASE, else origin/feature/ws-simplification,
# falling back to origin/main. GATES_SCOPE=full forces full (other values are
# ignored).
set -euo pipefail

# Only "full" overrides: a leftover GATES_SCOPE must never skip tests.
if [ "${GATES_SCOPE:-}" = full ]; then
	echo full
	exit 0
fi

base="${1:-${SCOPE_BASE:-origin/feature/ws-simplification}}"
if ! git rev-parse --verify -q "${base}^{commit}" >/dev/null; then
	if [ -z "${1:-}" ] && git rev-parse --verify -q "origin/main^{commit}" >/dev/null; then
		base="origin/main"
	else
		echo full
		exit 0
	fi
fi

# --no-renames: a moved file counts under its old path too (src/a.c moved to
# docs/ is still a src/ change). -z gives NUL-separated, unquoted paths (spaces
# and non-ASCII are C-quoted otherwise); porcelain -z entries are "XY path".
scope=docs
classify () {
	case "$1" in
	*.md|docs/*|.agents/*|.cursor/*|LICENSE*) ;;
	webui/*) scope=web ;;
	*) echo full; exit 0 ;;
	esac
}

tmpd="$(mktemp -d "${TMPDIR:-/tmp}/scope.XXXXXX")"
trap 'rm -rf "$tmpd"' EXIT
git diff -z --no-renames --name-only "${base}...HEAD" >"$tmpd/diff" || {
	echo full
	exit 0
}
git status --porcelain -z -uall --no-renames >"$tmpd/status" || {
	echo full
	exit 0
}
while IFS= read -r -d '' f; do
	[ -n "$f" ] && classify "$f"
done <"$tmpd/diff"
while IFS= read -r -d '' f; do
	[ -n "$f" ] && classify "${f:3}"
done <"$tmpd/status"
echo "$scope"
