#!/usr/bin/env bash
# Reproduce GitHub Actions c-tests locally (Ubuntu, no sound card, clean build).
#
# The dependencies are baked into a local image built from
# .github/docker/Dockerfile.ci-test (package list: ci-test-deps.txt). Docker's
# layer cache makes that step near-instant after the first run; set
# PIANOBAR_CI_REBUILD=1 to pull the base image and reinstall the packages
# (do that now and then so the toolchain follows the CI runner's).
#
# The host tree is mounted read-only and its files (tracked plus untracked,
# not ignored: `git ls-files -co --exclude-standard`) are copied into a
# container-local directory before building, so the run never writes to the
# host tree and no Linux objects or .gcda files land in it.
#
# PIANOBAR_CI_TARGET=<make target> runs that target instead of the default
#   coverage run (e.g. PIANOBAR_CI_TARGET=test-asan).
# PIANOBAR_CI_JOBS=N  in-container make -j (default: nproc of the Docker VM;
#   1 for a serial, non-interleaved build).
# PIANOBAR_CI_OUT=<dir>  copy coverage.info (coverage run) out to <dir>.
# PIANOBAR_CI_IMAGE=<image>  base image (default ubuntu:24.04).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BASE="${PIANOBAR_CI_IMAGE:-ubuntu:24.04}"
TAG="pianobar-ci:$(printf '%s' "$BASE" | tr -c 'A-Za-z0-9_.-' '-')"
TARGET="${PIANOBAR_CI_TARGET:-}"
JOBS="${PIANOBAR_CI_JOBS:-}"
OUT="${PIANOBAR_CI_OUT:-}"

echo "==> CI-local: image ${TAG} (base ${BASE})"
build_args=()
if [ -n "${PIANOBAR_CI_REBUILD:-}" ]; then
	build_args+=(--pull --no-cache)
fi
if ! docker build ${build_args[@]+"${build_args[@]}"} -q --build-arg BASE_IMAGE="${BASE}" \
		-t "${TAG}" -f .github/docker/Dockerfile.ci-test .github/docker >/dev/null; then
	echo "==> CI-local: image build failed; rerunning with full output" >&2
	docker build ${build_args[@]+"${build_args[@]}"} --progress=plain \
		--build-arg BASE_IMAGE="${BASE}" \
		-t "${TAG}" -f .github/docker/Dockerfile.ci-test .github/docker
	exit 1
fi

out_mount=()
if [ -n "${OUT}" ]; then
	mkdir -p "${OUT}"
	out_mount=(-v "$(cd "${OUT}" && pwd):/out")
fi

echo "==> CI-local: ${TARGET:-test-coverage} (PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1)"

# File list on stdin (NUL-separated); the container copies those files from
# the read-only /src mount into its own /build.
git ls-files -z -co --exclude-standard |
docker run --rm -i -e PIANOBAR_CI_TARGET="${TARGET}" -e PIANOBAR_CI_JOBS="${JOBS}" \
	${ASAN_OPTIONS:+-e ASAN_OPTIONS="${ASAN_OPTIONS}"} \
	${TEST_JOBS:+-e TEST_JOBS="${TEST_JOBS}"} \
	-v "${ROOT}:/src:ro" \
	${out_mount[@]+"${out_mount[@]}"} \
	-w /build \
	"${TAG}" \
	bash -ec '
		set -euo pipefail
		tar -C /src --null --ignore-failed-read -T - -cf - | tar -xf - -C /build
		export PIANOBAR_INTEGRATION=1
		export PIANOBAR_TEST_NO_DEVICE=1
		JOBS="${PIANOBAR_CI_JOBS:-$(nproc)}"
		make -j"${JOBS}" "${PIANOBAR_CI_TARGET:-test-coverage}"
		if [ -z "${PIANOBAR_CI_TARGET}" ]; then
			lcov --summary coverage.info --rc lcov_branch_coverage=1 \
				--ignore-errors inconsistent,unused 2>&1 | grep -E "^ *(lines|functions|branches)"
			if [ -d /out ]; then
				cp coverage.info /out/coverage.info
			fi
		fi
	'

echo "==> CI-local passed"
