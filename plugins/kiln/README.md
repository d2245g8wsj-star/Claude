# KILN — IndieWave Audio

A two channel variable-mu tube compressor. No ratio knob: like the tube
compressors it is modelled on, the ratio is a property of level, gentle at the
threshold and steepening as the signal goes past it. Around the gain cell sit a
calibrated tube THD stage and a choice of output: a tube line amplifier into a
transformer, or a clean solid state stage.

![The Kiln editor](docs/ui.png)

Built with JUCE 8. Ships as **VST3** and **Standalone** on Windows, macOS and
Linux, plus **AU** on macOS.

---

## Signal flow

```
in ─┬─ input trim ─┬─ vari-mu cell ─ THD (triode) ─ output stage ─┬─ mix ─ output trim ─ out
    │              │      ▲                          tube + iron  │   ▲
    │              │      │ gain reduction           or solid     │   │
    │              │   law ◄─ envelope ◄─ rectifier ◄─ SC HPF / HF │   │
    │              │      FF: from the input ─────┘   ▲            │   │
    │              │      FB: from the cell output ───┘            │   │
    │              └──────────────────────── dry (trimmed) ────────┴───┘
    └─ IN / bypass: delay matched, so switching out is the input exactly
```

Everything from the input trim to the mix runs at 4x.

## Controls

Per channel strip (A and B):

| Control | Range | What it does |
| --- | --- | --- |
| **Input** | ±20 dB, 0.5 dB steps | Level into the strip. Drives the cell, the THD stage and the iron. |
| **LINK** | | I/O link: turning the input moves the output the opposite way, so you change the drive without changing the level. |
| **Threshold** | −40 … 0 dBFS | Where the rectified control voltage starts biasing the tubes. |
| **Attack** | 0.5 … 100 ms | Rectifier timing capacitor, charge. |
| **Release** | 0.1 … 3 s | Rectifier timing capacitor, discharge. |
| **FF** | | Off: feedback, the detector listens to the cell's output (the classic vari-mu topology). On: feed-forward, it listens to the input. Same static curve, different movement. |
| **LIMIT** | | A steeper law: about 3:1 at the threshold, past 8:1 well over it. Works in both FB and FF. |
| **HPF** | 20 … 500 Hz | Sidechain high pass, 12 dB/oct, so the low end does not pump the mix. |
| **HF** + **IN** | 2 … 20 kHz | Sidechain high shelf, +6 dB, makes the detector more sensitive to the top. |
| **LISTEN** | | Plays the sidechain signal, to set the filters by ear. |
| **THD** | 0 … 8 % | Triode saturation, calibrated (see below). |
| **TUBE** | | On: tube line amp into the output transformer. Off: electronically balanced, clean. |
| **IRON** | | Iron pad: 10 dB more into the transformer core, 10 dB less after it. Same level, more iron. |
| **Mix** | 0 … 100 % | Parallel compression, against the trimmed input, sample aligned. |
| **Output** | ±20 dB, 0.5 dB steps | Level out of the strip. |
| **IN** | | Switches the whole strip, trims included. Out is the input, sample for sample. |

Global **Mode**: **Dual Mono** (A left, B right, independent detectors),
**Stereo** (A on both, one shared detector so the image cannot move; B greys
out) and **Mid/Side** (A on the mid, B on the side).

Each strip has a moving coil gain reduction meter with VU ballistics, and a
heater lamp in the COLOUR section that glows as the THD stage is driven.

## How it is modelled

**The gain law.** A remote cutoff tube's gain falls faster the further its grid
is pushed negative. With *e* the rectified level's excess over the threshold,
as a fraction of it, the reduction is

```
GR (dB) = K · (e + c · e²)
```

In the feedback loop this gives the progressive curve a vari-mu is known for.
Measured (1 kHz, threshold −30 dBFS, from `kiln_tests`):

| Input over threshold | 3 dB | 9 dB | 15 dB | 24 dB | 30 dB |
| --- | --- | --- | --- | --- | --- |
| Gain reduction | 0.9 dB | 3.9 dB | 7.8 dB | 14.6 dB | 19.5 dB |
| Local ratio | 1.4:1 | 2.2:1 | 3.1:1 | 4.7:1 | 5.8:1 |

Feed-forward uses the same law, solved the other way round, so both
topologies agree on the static curve to within a fraction of a dB and never
over-compress. LIMIT reaches about 10:1 at 12 to 24 dB over.

**The rectifier** is full wave with a timing capacitor, not an RMS detector, so
a fast attack on a low note ripples the gain, as it does on the hardware.

**The gain cell** is push-pull, so mostly odd harmonics with a little even from
imbalance. Its curvature grows with the reduction: an idle cell is exactly a
wire (−68 dB residual, the oversampling filters' ripple), a working one is not
(0.04 % THD at light reduction, 0.9 % at heavy).

**THD** is a biased triode curve. The knob is calibrated: it reads the
distortion the stage produces on a 1 kHz sine at the +4 dBu operating level
(−14 dBFS; 0 dBu = −18 dBFS throughout). 0.5 / 1 / 4 / 8 % measure
0.50 / 1.00 / 4.00 / 8.00 %. Louder material distorts more, quieter less.

**The output transformer** saturates on flux, the integral of the voltage, so a
loud bass note pushes the core far harder than the same level at 1 kHz. At
−2 dBFS: 0.9 % at 30 Hz against 0.4 % at 1 kHz (the latter is the tube line
amp), and 5.3 % at 30 Hz with the IRON pad in. Response with the tube output:
−0.5 dB at 20 Hz, +0.26 dB at 20 kHz from the leakage resonance.

Latency: 61 samples at every rate, reported to the host.

## What it is, and what it is not

The control set, ranges and topology follow the published description of a
well known hardware dual channel variable-mu compressor with digital recall:
six tubes and four transformers, feedback and feed-forward detection with a
limit mode, attack 0.5–100 ms, release 0.1–3 s, THD 0–8 %, a sidechain high
pass (20–500 Hz) and high frequency boost (2–20 kHz) with listen, a choice of
tube-and-transformer or electronically balanced output with an iron pad,
independent input and output gain with inverse linking, and dual mono, stereo
and mid/side operation.

It is **not** a measured model of that unit. No hardware was measured and the
maker's manual could not be consulted, so the following are this project's
own choices: the shape of the gain law and its constants, the 0 dBu = −18 dBFS
reference, the threshold range, the slope and gain of the sidechain filters,
the transformer and tube curves, and the **Mix** control, which is an addition.
The interface is an original design, and no name, logo or panel artwork of any
other product is used.

If you own the unit, the constants to adjust against it are at the top of
`src/dsp/VariMu.h` (law), `src/dsp/Tube.h` (THD reference level) and
`src/dsp/OutputStage.h` (iron and tube line amp).

## Building

Requires CMake 3.22+ and a C++20 compiler.

```bash
cd plugins/kiln
cmake -B build -DCMAKE_BUILD_TYPE=Release          # fetches JUCE 8.0.6
cmake --build build --parallel
```

On Linux, JUCE needs the usual development headers (see the Crest README).

### Offline checks and snapshot

```bash
cmake -B build -DKILN_BUILD_TESTS=ON -DKILN_BUILD_TOOLS=ON
cmake --build build --target kiln_tests kiln_shot --parallel
./build/kiln_tests_artefacts/Release/kiln_tests
./build/kiln_shot_artefacts/Release/kiln_shot docs/ui.png 1600
```

The checks assert: the THD table inverts, the idle cell is a wire, the ratio
rises with level and the output never falls as the input rises, FF and FB share
one static curve, LIMIT is harder, attack and release times scale, the THD knob
reads true, the iron saturates on bass and the pad drives it harder without a
level change, the sidechain filters and listen work, IN, bypass and mix at 0 %
null, stereo linking holds the image, the mid/side matrix reconstructs,
reported latency, finite output at extreme settings across rates and block
sizes, and state surviving a save.

## Source

```
src/dsp/VariMu.h        gain law, rectifier, gain cell, sidechain filter
src/dsp/Tube.h          triode curve, THD calibration, level reference
src/dsp/OutputStage.h   tube line amp, transformer, iron pad
src/dsp/Meters.h        audio thread to editor
src/PluginProcessor.*   parameters, routing, linking, oversampling, the strips
src/gui/Panel.*         knobs, lamps, mode buttons, VU meter, all drawn in code
src/PluginEditor.*      layout, I/O link, scaled as a whole from one design size
tests/Tests.cpp         the offline checks
tools/Snapshot.cpp      offscreen editor render
```

## Licence

As for the rest of this repository; see `../../LICENSE` and `../../NOTICE.md`.
