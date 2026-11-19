#!/usr/bin/env bash
# Reproduce GitHub Actions c-tests locally (Ubuntu, no sound card, clean build).
# PIANOBAR_CI_TARGET=<make target> runs that target after a distclean instead
# of the default coverage run (e.g. PIANOBAR_CI_TARGET=test-asan).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

IMAGE="${PIANOBAR_CI_IMAGE:-ubuntu:24.04}"
TARGET="${PIANOBAR_CI_TARGET:-}"

echo "==> CI-local: ${IMAGE} ${TARGET:-default} (PIANOBAR_INTEGRATION=1 PIANOBAR_TEST_NO_DEVICE=1)"

docker run --rm -e PIANOBAR_CI_TARGET="${TARGET}" \
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
		if [ -n "${PIANOBAR_CI_TARGET}" ]; then
			make distclean
			make "${PIANOBAR_CI_TARGET}"
		elif command -v lcov >/dev/null 2>&1; then
			make test-coverage
		else
			make distclean
			make test-all
		fi
	'

echo "==> CI-local passed"
