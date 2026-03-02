#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Default to local embedded-bridge checkout
: "${EMBEDDED_BRIDGE_DIR:=$(cd ../embedded-bridge && pwd)}"
export EMBEDDED_BRIDGE_DIR

# C++ tests
cmake -B build -S . -DBUILD_TESTING=ON \
    -DEMBEDDED_BRIDGE_DIR="$EMBEDDED_BRIDGE_DIR" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build
ctest --test-dir build --output-on-failure "$@"

# Python client tests
python/test.sh "$@"
