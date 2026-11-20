#!/usr/bin/env bash
# Tests for scripts/changed-scope.sh (run by `make test-scripts`).
# Each case builds a throwaway repo: branch `base`, then a topic branch with
# the case's files, and checks the printed scope.
set -euo pipefail
# The cases set their own overrides; ignore the caller's.
unset GATES_SCOPE SCOPE_BASE

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCOPE="${ROOT}/scripts/changed-scope.sh"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/changed-scope-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0

# new_repo: fresh repo in $TMP/r with one commit on branch `base`.
new_repo () {
	rm -rf "$TMP/r"
	mkdir -p "$TMP/r"
	cd "$TMP/r"
	git init -q -b base
	git config user.email t@example.invalid
	git config user.name t
	echo seed >seed.txt
	git add seed.txt
	git commit -q -m seed
	git checkout -q -b topic
}

# commit_files <path>...: create and commit each path.
commit_files () {
	for f in "$@"; do
		mkdir -p "$(dirname "$f")"
		echo x >"$f"
		git add "$f"
	done
	git commit -q -m change
}

# expect <case> <want> [args...]: run the classifier in $TMP/r.
expect () {
	local name="$1" want="$2" got
	shift 2
	got="$(cd "$TMP/r" && "$SCOPE" "$@" 2>/dev/null || echo "exit-$?")"
	if [ "$got" = "$want" ]; then
		pass=$((pass + 1))
	else
		echo "FAIL ${name}: want ${want}, got ${got}" >&2
		fail=$((fail + 1))
	fi
}

new_repo; commit_files README.md docs/x.md
expect md_only_is_docs docs base

new_repo; commit_files .agents/s/SKILL.md .cursor/rules/a.mdc
expect agents_and_cursor_is_docs docs base

new_repo; commit_files webui/src/a.ts
expect webui_only_is_web web base

new_repo; commit_files webui/src/a.ts README.md
expect webui_plus_md_is_web web base

new_repo; commit_files README.md src/a.c
expect mixed_docs_and_src_is_full full base

new_repo; commit_files locale/en.yaml
expect locale_is_full full base

new_repo; commit_files Makefile
expect makefile_is_full full base

new_repo; commit_files .github/workflows/test.yml
expect workflow_is_full full base

new_repo; commit_files a.md; mkdir -p src; echo y >src/a.c
expect uncommitted_src_is_full full base

new_repo
expect empty_diff_is_docs docs base

new_repo; commit_files README.md
export GATES_SCOPE=full
expect override_full full base
unset GATES_SCOPE

new_repo; commit_files README.md
expect missing_base_falls_back_full_or_main full no-such-ref

new_repo; mkdir -p src; echo c >src/foo.c; git add src/foo.c; git commit -q -m src
git checkout -q -b base2; git checkout -q topic; git mv src/foo.c docs/foo.md 2>/dev/null || { mkdir -p docs; git mv src/foo.c docs/foo.md; }
git commit -q -m mv
expect rename_src_to_docs_committed_is_full full base2

new_repo; mkdir -p src; echo c >src/foo.c; git add src/foo.c; git commit -q -m src
git checkout -q -b base2; git checkout -q topic; mkdir -p docs; git mv src/foo.c docs/foo.md
expect rename_src_to_docs_staged_is_full full base2

new_repo; commit_files src/a.c
export GATES_SCOPE=docs
expect override_docs_is_ignored full base
export GATES_SCOPE=web
expect override_web_is_ignored full base
unset GATES_SCOPE

echo "test-changed-scope: ${pass} passed, ${fail} failed"
[ "$fail" -eq 0 ]
