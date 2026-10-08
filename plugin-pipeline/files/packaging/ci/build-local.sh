#!/usr/bin/env bash
# The CI's build on your own machine: configure, build, test and pack into dist/, with the
# same settings (packaging/pipeline.conf). On a Mac it also runs auval.
#   bash packaging/ci/build-local.sh
set -euo pipefail
cd "$(dirname "$0")/../.."
source packaging/pipeline.conf

generator=()
command -v ninja > /dev/null && generator=(-G Ninja)
mac=()
if [ "$(uname -s)" = Darwin ]; then
    mac=("-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" "-DCMAKE_OSX_DEPLOYMENT_TARGET=${MACOS_MIN:-11.0}")
fi
# shellcheck disable=SC2086
# (The ${a[@]+...} form: macOS's bash 3.2 calls an empty array unbound under set -u.)
cmake -B "${BUILD_DIR:-build}" ${generator[@]+"${generator[@]}"} -DCMAKE_BUILD_TYPE=Release ${mac[@]+"${mac[@]}"} $CMAKE_ARGS
cmake --build "${BUILD_DIR:-build}" --config Release ${BUILD_TARGET:+--target "$BUILD_TARGET"}
if [ "${RUN_TESTS:-1}" = 1 ]; then
    ctest --test-dir "${BUILD_DIR:-build}" -C Release --output-on-failure --no-tests=ignore
fi
bash packaging/ci/package.sh
if [ "$(uname -s)" = Darwin ]; then
    bash packaging/ci/validate-macos.sh
fi
