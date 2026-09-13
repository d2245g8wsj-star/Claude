#!/usr/bin/env bash
# Build the ARA plug-in and drive it through an in-process ARA host.
# Fetches the ARA SDK on first run.
set -euo pipefail
cd "$(dirname "$0")"

BUILD=${1:-build}
PYTHON=${PYTHON:-../../.venv/bin/python}

cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release . >/dev/null
cmake --build "$BUILD" -j"$(nproc)" >/dev/null

# The harness needs a vocal to work on; reuse the core's reference take.
WAV="$BUILD/reference/voice.wav"
if [ ! -f "$WAV" ]; then
  mkdir -p "$BUILD/reference"
  PYTHONPATH=../.. "$PYTHON" ../tests/refgen.py "$BUILD/reference" >/dev/null
fi

"$BUILD/vocalai_ara_harness" "$WAV"
