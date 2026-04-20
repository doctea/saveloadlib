#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_BIN="${ROOT_DIR}/.pio/build/native_bench_manual/bench"

mkdir -p "$(dirname "${OUT_BIN}")"

echo "[bench] Building benchmark binary..."
g++ -std=gnu++17 -O2 \
  -DSL_BENCH_MAIN \
  -I"${ROOT_DIR}/include" \
  -I"${ROOT_DIR}/test/native/stubs" \
  "${ROOT_DIR}/test/native/test_benchmark.cpp" \
  "${ROOT_DIR}/test/native/saveload_test_stubs.cpp" \
  "${ROOT_DIR}/src/saveloadlib.cpp" \
  -o "${OUT_BIN}"

echo "[bench] Running benchmark..."
"${OUT_BIN}"
