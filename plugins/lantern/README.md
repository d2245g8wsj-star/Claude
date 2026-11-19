# LANTERN — IndieWave Audio

A stereo four band passive tube equaliser, modelled on a well known two
channel hardware unit with digital recall from the same maker as the
vari-mu Beacon follows, and dressed to sit beside Beacon in a rack. Four
inductor bands of twelve positions each, proportional or constant Q, a
moderate or extended gain range, active high and low pass filters, a
calibrated THD stage, and a choice of output: RED (tube amplifier,
transformer and IRON PAD) or GREEN (clean). The centre of the panel draws
the response.

![The Lantern editor](docs/ui.png)

Built with JUCE 8. Ships as **VST3** and **Standalone** on Windows, macOS and
Linux, plus **AU** on macOS.

---

## Signal flow

```
in ─┬─ [M/S encode] ─ HPF ─ LPF ─ LF ─ LMF ─ HMF ─ HF ─ THD ─ output ─ [M/S decode] ─┬─ GREEN ───────────────┬─ out
    │                (active)     (passive inductor bands)                              └─ RED: tube ─ iron ─ PAD┘
    └─ bypass: delay matched, so switching out is the input exactly
```

Everything from the filters to the output stage runs at 4x.

## Controls

Per channel panel:

| Band | Positions | Gain | Switches |
| --- | --- | --- | --- |
| **Low** | 22, 30, 40, 60, 80, 100, 150, 220, 330, 470, 680 Hz, 1 kHz | ±15 dB (or ±5) | IN, BELL (off: shelf) |
| **Low mid** | 90, 130, 180, 270, 390, 560, 820 Hz, 1.2, 1.8, 2.7, 3.9, 4.8 kHz | ±15 dB (or ±5) | IN, HI-Q |
| **High mid** | 260, 380, 560, 820 Hz, 1.2, 1.6, 2.2, 3.3, 4.7, 6.8, 8.2, 11 kHz | ±15 dB (or ±5) | IN, HI-Q |
| **High** | 600 Hz, 1, 1.5, 2.2, 3.3, 4.7, 6.8, 10, 12, 16, 20, 28 kHz | ±15 dB (or ±5) | IN, BELL (off: shelf) |

| Control | Range | What it does |
| --- | --- | --- |
| **HPF** + **IN** + **24** | 12 … 300 Hz | Active high pass, 12 dB/oct, or 24 with the key in. Butterworth. |
| **LPF** + **IN** + **24** | 6 … 50 kHz | Active low pass, the same. |
| **THD** | 0 … 100 | A saturation stage after the bands, so boosts drive it. A square law to 8 % at +4 dBu: 25 is 0.5 %, 50 is 2 %. |
| **Output** | ±15 dB, 0.25 dB steps | In RED mode it is what drives the tube and the iron. |

In the centre, for the whole unit:

| Control | What it does |
| --- | --- |
| **RANGE** | Lit: ±15 dB. Out: moderate, ±5 dB — the same knob position is worth a third, as a range switch on hardware changes what a knob does. Hosts show the knob's ±15 dB value either way. |
| **CONST Q** | Out: proportional Q, the bells narrow as they are pushed (broad for small moves, nominal at 10 dB). Lit: one width at every gain. Shelves are not affected. |
| **IRON** | RED: tube line amp, output transformer and PAD (IRON PAD with its LINK to Output, on the first panel). Off: GREEN. |
| **BYPASS** | The host's bypass, on the panel. |

**Mode**, as on Beacon: **Stereo** is one set of settings for both channels;
**Dual Mono** with the tab pulled (**SPLIT L/R**) folds out a right panel
with its own; **Mid/Side** equalises mid and side separately. The curve
shows the first panel in green and the second in amber. The output
transformers work in left/right, outside the M/S matrix.

### Measured (from `lantern_tests`, 48 kHz)

| | |
| --- | --- |
| All bands at 0, filters out, 30 Hz – 15 kHz | within 0.02 dB |
| THD+N at +4 dBu, GREEN | 0.0004 % (specified: 0.0025 %) |
| THD at +4 dBu, RED | 0.041 % (specified: 0.04 %) |
| Sections against their analogue prototypes | within 0.06 dB to 10 kHz, 0.35 dB to 20 kHz |
| Bell width at half its boost, proportional, 3 / 12 dB | 3.0 / 1.75 octaves |
| Same, constant / HI-Q at 12 dB | 1.9 / 0.7 octaves |
| HPF at 100 Hz, two octaves down, 12 / 24 dB | −24.1 / −48.2 dB |
| Latency | 61 samples at every rate |

## What it is, and what it is not

**From the hardware's published specifications and descriptions:** four
passive inductor bands of twelve positions each over 22 Hz to 28 kHz, with
the ranges of each band (22 Hz–1 kHz, 90 Hz–4.8 kHz, 260 Hz–11 kHz,
600 Hz–28 kHz); bands that can be switched off one by one; proportional or
constant Q; a moderate (±5 dB) or extended (±15 dB) gain range; active high
and low pass filters of 12 or 24 dB/oct, the high pass from about 12 to
300 Hz and the low pass from 6 to 50 kHz, each bypassable; a THD control of
a hundred steps; the RED output with tube, transformer and IRON PAD and the
clean GREEN output; stereo, dual mono and mid/side; and the THD+N figures
quoted above.

**This project's own choices,** because no hardware was measured and the
manual could not be consulted in full: the twelve frequencies inside each
band's range; that the outer bands are shelves with a BELL option and the
inner ones bells with HI-Q; every Q, and how proportional Q depends on gain;
the THD law; continuous rather than stepped filter frequencies; where THD
sits in the chain; and the output trim. All are constants at the top of
`src/dsp/Eq.h`, `src/PluginProcessor.h` and `src/dsp/OutputStage.h`.

The interface is an original design in the IndieWave style; no name, logo or
panel artwork of any other product is used.

## Building

Requires CMake 3.22+ and a C++20 compiler.

```bash
cd plugins/lantern
cmake -B build -DCMAKE_BUILD_TYPE=Release          # fetches JUCE 8.0.6
cmake --build build --parallel
```

On Linux, JUCE needs the usual development headers (see the Crest README).

### Offline checks and snapshot

```bash
cmake -B build -DLANTERN_BUILD_TESTS=ON -DLANTERN_BUILD_TOOLS=ON
cmake --build build --target lantern_tests lantern_shot --parallel
./build/lantern_tests_artefacts/Release/lantern_tests
./build/lantern_shot_artefacts/Release/lantern_shot docs/ui.png 1.0 stereo   # or dual, ms
```

The checks assert: every band rises at every step and spans its published
range; every section matches its analogue prototype and a section at 0 dB is
a wire; a flat, clean idle; bands switched out are flat; the bells reach
their gain at their centre and the shelves lift or cut the right end; BELL
turns a shelf into a bell; the moderate range divides by three; proportional
Q narrows with gain, constant Q does not, HI-Q is narrower, and the mode
reaches the audio; both filters at their corner and slope, and inert when
out; the THD calibration and the tube output's distortion; the iron driven
harder at the same level and the PAD inert in GREEN; bypass; mid/side, the
side's own settings, the iron in left/right; dual mono split; latency;
finite output at extreme settings and under random automation; a frequency
switch gliding without a click; state; and that sessions saved by released
versions (`tests/compat/`) open unchanged.

## Source

```
src/dsp/Eq.h            band positions, Q laws, the filter sections and their prototypes
src/dsp/Tube.h          triode curve, THD calibration, level reference (as Beacon)
src/dsp/OutputStage.h   transformer, tube line amp, RED/GREEN, iron pad (as Beacon)
src/dsp/Meters.h        audio thread to editor
src/PluginProcessor.*   parameters, range and Q switches, channel modes, oversampling
src/PluginEditor.*      the panel, the response curve, iron link, folding
tests/Tests.cpp         the offline checks
tools/Snapshot.cpp      offscreen editor render
```

## Licence

As for the rest of this repository; see `../../LICENSE` and `../../NOTICE.md`.
