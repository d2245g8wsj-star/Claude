# VocalAI native core (C++)

A dependency-free C++17 port of the Python engine, intended to be the DSP core
of an ARA 2 plug-in. It builds a static library plus a CLI, and its test suite
checks it against reference vectors produced by the Python implementation —
because agreement with the validated reference is the bar, not merely that the
C++ runs.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build -j
./build/vocalai_cli tune lead.wav lead_tuned.wav --style natural
```

## What is here

| file | contents |
| --- | --- |
| `fft.cpp` | radix-2 FFT with a Bluestein fallback, so any length works |
| `filters.cpp` | Butterworth design, `sosfilt`, `sosfiltfilt` |
| `dsp.cpp` | framing, interpolation, NaN-aware smoothing and medians |
| `wav.cpp` | RIFF/WAVE read and write, 8–32 bit PCM and float |
| `pitch.cpp` | pYIN-style tracking: CMNDF, threshold prior, Viterbi |
| `features.cpp` | spectrogram, onset envelope, loudness |
| `notes.cpp` | note segmentation, cores, vibrato measurement |
| `key.cpp` | Krumhansl–Kessler key detection, concert-pitch estimation |
| `tuner.cpp` | target decode and the correction curve |
| `psola.cpp` | phase-locked epochs, TD-PSOLA, transient-aware WSOLA |
| `engine.cpp` | track-level API — the surface an ARA controller drives |
| `model.cpp` | the editable note model, as JSON |

## Verification

The tests need reference vectors, generated once from the Python engine:

```bash
PYTHONPATH=.. python tests/refgen.py build/reference
VOCALAI_REF_DIR=build/reference ./build/vocalai_tests
```

Measured agreement with the Python reference on a synthesised sung phrase:

| stage | agreement |
| --- | --- |
| FFT (any length) | 1e-13 against numpy |
| Butterworth `sosfilt` / `sosfiltfilt` | 1e-9 against scipy |
| Framing, interpolation, medians | 1e-10 against numpy |
| Pitch tracking | 4e-12 cents; identical voicing on every frame |
| Note segmentation | same note count; centres to 1e-6 cents |
| Pitch marks | identical, to the sample |
| Correction curve | 4e-11 cents |
| Rendered audio | −72 to −79 dB RMS difference; output pitch identical to 0.00 cents |

That last row is worth reading carefully. The renders are *not* sample
identical, and they never will be: the PSOLA loop accumulates its output
position in floating point, so a grain occasionally lands one sample either
side of where the Python put it. What matters is that the difference is
inaudible and carries no pitch error — both renders were measured back and
agree to 0.00 cents.

## Performance

About 12x realtime for analysis on one core (a 4-minute vocal in roughly 20
seconds), against 9x for the Python. The gap is smaller than it looks: numpy's
FFT is heavily optimised C, so the port wins mainly on the loops around it.
The obvious next step is a split real/imaginary FFT kernel — `std::complex`
vectorises poorly — which should be worth another 1.5–2x.

## Status

This is the engine. The ARA 2 plug-in built on top of it lives in
[`ara/`](ara/) and is validated against an in-process ARA host; there is no
companion API wrapper (`.vst3` / `.component`) yet, and nothing here has been
compiled or run on macOS.
