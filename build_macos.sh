#!/usr/bin/env bash
set -euo pipefail

BUILD_TYPE=${1:-Release}

mkdir -p build

cmake -S . -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build build -j

echo ""
echo "Done"
