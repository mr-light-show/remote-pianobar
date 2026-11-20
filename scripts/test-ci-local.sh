#!/usr/bin/env bash
# Reproduce GitHub Actions c-tests locally (Ubuntu, no sound card, clean build).
# PIANOBAR_CI_TARGET=<make target> runs that target after a distclean instead
# of the default coverage run (e.g. PIANOBAR_CI_TARGET=test-asan).
# The in-container build uses every CPU the Docker VM has (nproc); set
# PIANOBAR_CI_JOBS=1 for a serial, non-interleaved build.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

IMAGE="${PIANOBAR_CI_IMAGE:-ubuntu:24.04}"
TARGET="${PIANOBAR_CI_TARGET:-}"
JOBS="${PIANOBAR_CI_JOBS:-}"

echo "==> CI-local: ${IMAGE} ${TARGET:-default} (PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1)"

docker run --rm -e PIANOBAR_CI_TARGET="${TARGET}" -e PIANOBAR_CI_JOBS="${JOBS}" \
	-v "${ROOT}:/work" \
	-w /work \
	"${IMAGE}" \
	bash -ec '
		set -euo pipefail
		export DEBIAN_FRONTEND=noninteractive
		apt-get update -qq
		apt-get install -y -qq make gcc pkg-config check lcov python3 python3-yaml \
			libao-dev libavcodec-dev libavformat-dev libavutil-dev \
			libavfilter-dev libjson-c-dev libgcrypt20-dev \
			libcurl4-openssl-dev libwebsockets-dev libasound2-dev \
			libblas-dev liblapack-dev >/dev/null
		export PIANOBAR_INTEGRATION=1
		export PIANOBAR_TEST_NO_DEVICE=1
		JOBS="${PIANOBAR_CI_JOBS:-$(nproc)}"
		if [ -n "${PIANOBAR_CI_TARGET}" ]; then
			make distclean
			make -j"${JOBS}" "${PIANOBAR_CI_TARGET}"
		elif command -v lcov >/dev/null 2>&1; then
			make -j"${JOBS}" test-coverage
		else
			make distclean
			make -j"${JOBS}" test-all
		fi
	'

echo "==> CI-local passed"
