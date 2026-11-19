# ANCHOR — IndieWave Audio

A stereo VCA bus compressor, modelled on a well known two channel hardware
unit with digital recall from the same maker as the vari-mu Beacon follows,
and dressed to sit beside Beacon in a rack: black panels, knurled knobs in
rings of LEDs, a moving coil meter between level ladders. Stepped ratio,
attack and release with AUTO, sidechain high passes and two tilt curves, an
external key, a calibrated THD stage, parallel mix, and a choice of output:
RED (tube amplifier, transformer and IRON PAD) or GREEN (clean).

![The Anchor editor](docs/ui.png)

Built with JUCE 8. Ships as **VST3** and **Standalone** on Windows, macOS and
Linux, plus **AU** on macOS.

---

## Signal flow

```
in ─┬─ [M/S encode] ─ input ─┬─ THD ─ VCA ─────────────┬─ mix ─ output ─ [M/S decode] ─┬─ GREEN ───────────────┬─ out
    │                        │         ▲               │   ▲                            └─ RED: tube ─ iron ─ PAD┘
    │                        │   ballistics ◄ curve ◄ SC filter ◄─ FF: the cell's input (or the EXT key)
    │                        │                          ◄─ FB: the cell's output
    │                        └──────────────── dry ─────────┘
    └─ bypass: delay matched, so switching out is the input exactly
```

Everything from the input trim to the output stage runs at 4x.

## Controls

| Control | Range | What it does |
| --- | --- | --- |
| **Threshold** | −20 … +20 dBu | Where the corner sits. 0 dBu is −18 dBFS. |
| **Ratio** | 1.5, 2, 4, 10:1, INF | Stepped. INF is a limiter. |
| **Attack** | 0.1, 0.3, 1, 3, 10, 30 ms | Stepped. |
| **Release** | 0.1, 0.3, 0.6, 0.9, 1.2 s, AUTO | Stepped. AUTO is program dependent, see below. |
| **FEED BACK** | | Off: feed-forward, the detector listens to the cell's input (the default for a VCA bus compressor). On: it listens to the cell's output, which is softer on transients. Both land on the same static curve. |
| **S.C. FILTER** | OFF, 60, 90, 150 Hz, T1, T2 | What the detector hears. The three high passes are 6 dB/oct; T1 and T2 are tilts that also make it listen harder to the top. |
| **LISTEN** | | Plays the sidechain, filter included. |
| **EXT KEY** | | The detectors listen to the plugin's sidechain input instead of the programme. Without a key connected it does nothing. |
| **Input** | ±15 dB, 0.25 dB steps | Level into the unit. |
| **DYNAMIC LINK** | | Turning Input or Threshold moves the other the same way and Output the opposite way, so the drive changes and the level does not. |
| **THD** | 0 … 8 % | A saturation stage on the wet path only, ahead of the VCA, calibrated at +4 dBu. |
| **Mix** | 0 … 100 % | Parallel compression, sample aligned. The dry half is taken after the input trim. |
| **Output** | ±15 dB, 0.25 dB steps | After the mix. In RED mode it is what drives the tube and the iron. |
| **IRON** | | RED: tube line amp, output transformer and PAD. Off: GREEN, clean. |
| **IRON PAD** + **LINK** | 0 … 15 dB | Passive attenuator after the iron, RED only; the link moves it with Output. |
| **BYPASS** | | The host's bypass, on the panel. |

**Mode**, as on Beacon: **Stereo** links the two detectors so the image
cannot shift; **Dual Mono** gives each channel its own detector, and the tab
at the edge (**SPLIT L/R**) folds out a right panel with its own settings;
**Mid/Side** compresses mid and side separately, each with its own panel.
IRON, PAD, its link and EXT KEY are single, on the first panel; the output
transformers work in left/right, outside the M/S matrix.

### AUTO release

Two recoveries in parallel: a fast one (80 ms) for what a transient took, and
a slow one (1.6 s) that charges only from material held for a while (0.35 s).
The cell takes whichever asks for more. From the checks: after a 20 ms burst
the reduction is half way back in 56 ms; after 3 s of the same tone it takes
722 ms.

### Measured (from `anchor_tests`, 48 kHz)

| | |
| --- | --- |
| 20 dB over the threshold, FF: 1.5 / 2 / 4 / 10 / INF | 6.6 / 9.9 / 14.9 / 17.8 / 19.8 dB of reduction |
| Same, FB | 6.6 / 9.9 / 14.9 / 17.9 / 20.0 dB |
| Attack to half way: 1 / 3 / 10 / 30 ms switch | 2 / 4 / 10 / 29 ms |
| Release to half way: 0.1 / 0.6 / 1.2 s switch | 70 / 415 / 789 ms |
| THD+N at rest, +4 dBu, GREEN | 0.0004 % |
| Same, with 7.5 dB of reduction | 0.06 % |
| THD knob 0.5 / 2 / 8 % at +4 dBu | 0.50 / 2.00 / 8.00 % |
| 40 Hz, 20 dB over: SC filter off / 150 Hz | 13.6 / 5.1 dB of reduction |
| Latency | 61 samples at every rate |

## What it is, and what it is not

**From the hardware's published specifications and descriptions:** the
ratio, attack and release positions including AUTO; the sidechain filter
positions OFF, 60, 90 and 150 Hz at 6 dB/oct, and that T1 and T2 are tilt
curves; the external sidechain; a THD control of 0 to 8 % that works on the
wet signal only; parallel mix; the RED output with tube, transformer, IRON
PAD and its link to Output, and the clean GREEN output; stereo, dual mono
and mid/side.

**This project's own choices,** because no hardware was measured and the
manual could not be consulted in full: the threshold range and level
reference (0 dBu = −18 dBFS); the 4 dB soft corner; the exact curves of T1
(90 Hz high pass with +3 dB above 3 kHz) and T2 (150 Hz with +6 dB above
5 kHz); how AUTO behaves inside; the trace of second harmonic the VCA adds as
it works; the feedback mode's capped loop gain at INF; the input and output
trims, dynamic link and where the dry tap sits (taken from Beacon, the same
maker's vari-mu). All are constants at the top of `src/dsp/Vca.h`,
`src/dsp/Tube.h` and `src/dsp/OutputStage.h`.

The interface is an original design in the IndieWave style; no name, logo or
panel artwork of any other product is used.

## Building

Requires CMake 3.22+ and a C++20 compiler.

```bash
cd plugins/anchor
cmake -B build -DCMAKE_BUILD_TYPE=Release          # fetches JUCE 8.0.6
cmake --build build --parallel
```

On Linux, JUCE needs the usual development headers (see the Crest README).

### Offline checks and snapshot

```bash
cmake -B build -DANCHOR_BUILD_TESTS=ON -DANCHOR_BUILD_TOOLS=ON
cmake --build build --target anchor_tests anchor_shot --parallel
./build/anchor_tests_artefacts/Release/anchor_tests
./build/anchor_shot_artefacts/Release/anchor_shot docs/ui.png 1.0 stereo   # or dual, ms
```

The checks assert: the static curve for every ratio in both detector modes;
silence below the threshold; a flat, clean idle; attack and release that
scale with their switches; AUTO letting a transient go quickly and sustained
material slowly; every sidechain filter position against its response, by
listening; the high pass keeping bass from pumping; the external key, and
falling back to the programme without one; the THD calibration and THD on the
wet path only; the iron driven harder at the same level and the PAD inert in
GREEN; bypass and dry mix; stereo linking, the mid/side matrix, the side's
own controls, the iron in left/right; dual mono split; latency; feedback at
INF settling; finite output at extreme settings across rates and block
sizes; state; and that sessions saved by released versions (`tests/compat/`)
open unchanged.

## Source

```
src/dsp/Vca.h           ratio law, ballistics and AUTO, the VCA, sidechain filter
src/dsp/Tube.h          triode curve, THD calibration, level reference (as Beacon)
src/dsp/OutputStage.h   transformer, tube line amp, RED/GREEN, iron pad (as Beacon)
src/dsp/Meters.h        audio thread to editor
src/PluginProcessor.*   parameters, channel modes, sidechain bus, oversampling, the signal path
src/PluginEditor.*      the panel, dynamic and iron links, folding
tests/Tests.cpp         the offline checks
tools/Snapshot.cpp      offscreen editor render
```

## Licence

As for the rest of this repository; see `../../LICENSE` and `../../NOTICE.md`.
