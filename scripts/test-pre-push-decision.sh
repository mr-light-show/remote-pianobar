#!/usr/bin/env bash
# Tests for scripts/pre-push-decision.sh (run by `make test-scripts`).
# Each case feeds pre-push stdin lines ("<local ref> <local sha> <remote ref>
# <remote sha>") in a throwaway repo and checks the decision.
set -euo pipefail
unset GATES_SCOPE SCOPE_BASE

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DECIDE="${ROOT}/scripts/pre-push-decision.sh"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/pre-push-decision-test.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
Z=0000000000000000000000000000000000000000

pass=0
fail=0

# new_repo: repo with branch base (seed), topic (docs-only commit), and
# other (a src/ commit on top of base). topic is checked out.
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
	git checkout -q -b other
	mkdir -p src
	echo c >src/a.c
	git add src/a.c
	git commit -q -m src
	git checkout -q -b topic base
	echo d >README.md
	git add README.md
	git commit -q -m docs
	echo build/ >.git/info/exclude
}

# expect <case> <want> <stdin>
expect () {
	local name="$1" want="$2" input="$3" got
	got="$(cd "$TMP/r" && printf '%s' "$input" | SCOPE_BASE=base "$DECIDE" 2>/dev/null || echo "exit-$?")"
	if [ "$got" = "$want" ]; then
		pass=$((pass + 1))
	else
		echo "FAIL ${name}: want ${want}, got ${got}" >&2
		fail=$((fail + 1))
	fi
}

sha_of () { git -C "$TMP/r" rev-parse "${1:-HEAD}"; }

new_repo
expect head_docs_branch_is_docs docs "refs/heads/topic $(sha_of) refs/heads/topic $Z
"

new_repo
expect other_ref_than_head_is_full full "refs/heads/other $(sha_of other) refs/heads/other $Z
"

new_repo
expect other_ref_onto_feature_is_full full "refs/heads/other $(sha_of other) refs/heads/feature/ws-simplification $Z
"

new_repo
expect head_plus_other_ref_is_full full "refs/heads/topic $(sha_of) refs/heads/topic $Z
refs/heads/other $(sha_of other) refs/heads/other $Z
"

new_repo
expect delete_only_is_docs docs "(delete) $Z refs/heads/gone $(sha_of other)
"

new_repo
expect no_stdin_classifies_head docs ""

new_repo; git -C "$TMP/r" checkout -q other
mkdir -p "$TMP/r/build/gates"; sha_of >"$TMP/r/build/gates/passed"
expect stamp_matches_head_is_stamp stamp "refs/heads/other $(sha_of) refs/heads/other $Z
"

new_repo; git -C "$TMP/r" checkout -q other
mkdir -p "$TMP/r/build/gates"; sha_of >"$TMP/r/build/gates/passed"; echo x >>"$TMP/r/src/a.c"
expect stamp_with_dirty_tree_is_full full "refs/heads/other $(sha_of) refs/heads/other $Z
"

new_repo; git -C "$TMP/r" checkout -q other
mkdir -p "$TMP/r/build/gates"; echo deadbeef >"$TMP/r/build/gates/passed"
expect stale_stamp_is_full full "refs/heads/other $(sha_of) refs/heads/other $Z
"

echo "test-pre-push-decision: ${pass} passed, ${fail} failed"
[ "$fail" -eq 0 ]
