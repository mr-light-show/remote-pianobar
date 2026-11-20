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
# docs/ is still a src/ change).
files="$( (git diff --no-renames --name-only "${base}...HEAD" &&
	git status --porcelain -uall --no-renames | cut -c4-) | sort -u)" || {
	echo full
	exit 0
}

scope=docs
while IFS= read -r f; do
	[ -n "$f" ] || continue
	case "$f" in
	*.md|docs/*|.agents/*|.cursor/*|LICENSE*) ;;
	webui/*) scope=web ;;
	*) echo full; exit 0 ;;
	esac
done <<<"$files"
echo "$scope"
