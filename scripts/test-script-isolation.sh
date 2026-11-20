#!/usr/bin/env bash
# The scripts/test-*.sh suites build throwaway repos. Git exports GIT_DIR (and
# friends) to hooks, and the pre-push hook runs these suites via test-all, so
# they must never act on the caller's repository. This runs each suite with
# GIT_DIR pointing at a scratch repo and checks that repo is untouched.
set -euo pipefail
# This suite itself runs inside the pre-push hook: drop git's local
# environment (GIT_DIR, ...) before creating the scratch repo.
# shellcheck disable=SC2046
unset $(git rev-parse --local-env-vars)

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
S="$(mktemp -d "${TMPDIR:-/tmp}/script-isolation.XXXXXX")"
trap 'rm -rf "$S"' EXIT
git -C "$S" init -q
if [ "$(git -C "$S" rev-parse --absolute-git-dir)" != "$(cd "$S" && pwd -P)/.git" ]; then
	echo "refusing to run: git does not point at the scratch repo" >&2
	exit 2
fi
git -C "$S" -c user.name=iso -c user.email=iso@example.invalid commit -q --allow-empty -m init

fail=0
for suite in test-changed-scope.sh test-pre-push-decision.sh; do
	before="$(git -C "$S" rev-parse HEAD) $(git -C "$S" branch --list | tr -d ' \n')"
	(cd "${TMPDIR:-/tmp}" && GIT_DIR="$S/.git" bash "${ROOT}/scripts/${suite}" >/dev/null 2>&1) || true
	after="$(git -C "$S" rev-parse HEAD) $(git -C "$S" branch --list | tr -d ' \n')"
	if [ "$before" != "$after" ] || git -C "$S" config --local --get user.name >/dev/null ||
			[ "$(git -C "$S" config --get core.bare)" != false ]; then
		echo "FAIL ${suite}: changed the caller's repository (GIT_DIR)" >&2
		fail=1
	fi
done
echo "test-script-isolation: $([ "$fail" -eq 0 ] && echo ok || echo FAILED)"
[ "$fail" -eq 0 ]
