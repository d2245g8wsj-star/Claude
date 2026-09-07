#!/usr/bin/env bash
# Build the native core and check it against the Python reference vectors.
set -euo pipefail
cd "$(dirname "$0")"

BUILD=${1:-build}
PYTHON=${PYTHON:-../.venv/bin/python}

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release . >/dev/null
cmake --build "$BUILD" -j"$(nproc)" >/dev/null

if [ ! -d "$BUILD/reference" ]; then
  echo "generating reference vectors from the Python engine..."
  PYTHONPATH=.. "$PYTHON" tests/refgen.py "$BUILD/reference"
fi

VOCALAI_REF_DIR="$BUILD/reference" "$BUILD/vocalai_tests"
