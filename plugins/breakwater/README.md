# BREAKWATER — IndieWave Audio

A mastering limiter. A compressor and a transient, detail and balance stage in
front, two limiting engines with eight styles, a true peak stage behind,
linear phase oversampling to 32x, and the loudness metering a master is judged
by. Named for what it is: the wall the swell breaks on, so the crest never
reaches the shore.

![The Breakwater editor](docs/ui.png)

Built with JUCE 8. Ships as **VST3** and **Standalone** on Windows, macOS and
Linux, plus **AU** on macOS.

---

## Signal flow

```
in ─┬─────────────────────────────────────── dry (delay matched) ───────────────┐
    │                                                                           │
    └─ gain ÷ceiling ─ DC ─ compressor ─ transients/low punch/detail ─ leveller ─┐
                                                                                │
         ┌────────────────── oversampled 1x … 32x ─────────────────────────────┘
         │            sidechain (optional) ── detectors
         └─ clip ─ Modern: lookahead peak limiter ─┐
                   Analog: zero lookahead limiter ─┴─ balance ─ down ─ true peak stage ─ ×ceiling ─ delta ─ match ─ dither ─ out
```

Everything between the gain and the ceiling runs in a normalised domain where
**1.0 is the ceiling**, so every stage works the same at any output level.

## Controls

### Limiter

| Control | Range | What it does |
| --- | --- | --- |
| **Gain** | −12 … +30 dB | How hard the signal is driven into the ceiling. |
| **Ceiling** | −24 … 0 dB | The level the output does not cross: sample peak, or true peak with **True Peak** on. |
| **Lookahead** | 0 … 10 ms | How far ahead the Modern engine sees. This is also its latency. |
| **Attack** | 0.01 … 10 ms | The length of the gain ramp in front of a peak, inside the lookahead. Analog engine: the charge time of its detector. |
| **Release** | 1 … 1000 ms | The base release. Every style moves it with the programme (below). |
| **Window** | Off, C4 … C0 | Holds the detector for a quarter of the period of a note, from C4 (about 1 ms) to C0 (about 15 ms), before the release starts. A fast limiter otherwise follows the individual cycles of a bass note, which is heard as distortion: with C0, the 3rd harmonic on a 50 Hz note driven 9 dB into the Analog engine falls from −27 to below −150 dBc. Works in both engines. |
| **Link Trans** | 0 … 100 % | Stereo link for the gain going down. |
| **Link Rel** | 0 … 100 % | Stereo link for the gain coming back. Separate, so peaks can be caught per side while the recovery stays centred. |
| **Adaptive Link** | on / off | Raises the transient link towards 100 % as the channels' correlation rises above 0.5, so a near mono mix holds its centre while a wide one keeps the independence Link Trans gives it. |

### Compressor

A feed forward RMS compressor in front of everything else, for the body of
the mix rather than its peaks.

| Control | Range | What it does |
| --- | --- | --- |
| **Comp In** | on / off | |
| **Threshold** | −30 … +6 dB | Relative to the ceiling, like every level in the chain. |
| **Ratio** | 1:1 … 10:1 | Soft 6 dB knee. |
| **Attack / Release** | 0.1 … 100 ms / 10 … 2000 ms | The release lengthens up to three times as the reduction deepens, which keeps hard compression from pumping. |
| **SC HPF** | Off, 20 … 300 Hz | High pass on the detector only, so the kick does not duck the mix. |
| **Mode** | Stereo, M/S | Linked on the louder side, or mid and side compressed independently. |

### Engine and style

| Engine | |
| --- | --- |
| **Modern** | A lookahead limiter that is brickwall by construction, preceded by a leveller. Eight styles. |
| **Analog** | No lookahead at all. It reacts to each sample as it arrives, the way a circuit does, so the front of a transient passes with its shape. A rectifier charges two capacitors: the fast one through a diode (program dependent attack), the slow one from the fast one (program dependent release). What gets through is caught by the true peak stage, which is short and only shaves fractions of a dB. |

| Style | Leveller | Peak stage |
| --- | --- | --- |
| **Clean** | 2:1, 3 dB above the ceiling | Full ramp, adaptive release. Transparent. |
| **Punch** | 1.6:1, slow to start | Short ramp, fast release: drum fronts arrive intact. |
| **Open** | off | Short ramp. The most dynamic. |
| **Loud** | 3:1, fast | Quick release. Density first. |
| **Balanced** | 2.5:1 | The all rounder. |
| **Glue** | 4:1, slow | Does most of the work, like a bus compressor. |
| **Safe** | off | Longest ramp, slowest release. Hardest to make distort. |
| **Smooth** | 2:1, gentle | Release rides on the programme density. |

### Shape

| Control | Range | What it does |
| --- | --- | --- |
| **Transients** | −100 … +100 % | A differential envelope transient shaper before the limiter. Positive settings push the onsets up so they still stand out after limiting; negative settings soften them. |
| **Low Punch** | −100 … +100 % | The same shaper on the low band only, split at 150 Hz by a fourth order Linkwitz-Riley crossover, whose bands are in phase so shaping changes level, not phase. Moves the kick's attack against its body by +6.8 / −4.5 dB at the extremes without touching snare and cymbals. |
| **Detail** | 0 … 100 % | Upward compression: lifts the quiet events between the hits (tails, ghost notes) by up to 6 dB, judged from a short against a long RMS. Never lifts anything more than 50 dB below the programme, so silence stays silent. |
| **Balance** | Lows … Highs | Re-weights the limiter's reduction between the band below 200 Hz and the rest. Towards Lows the low end takes more of it and the top stays bright; towards Highs the low end stays full. The split runs in double precision at the oversampled rate; the true peak stage catches what the re-weighting leaves above the ceiling. |
| **Clip** | 0 … 100 % | An oversampled soft clipper in front of the limiter that takes the first dB off the fastest peaks, so the limiter has less to do. |

### Output

| Control | What it does |
| --- | --- |
| **True Peak** | The final stage limits on an 8x interpolated estimate of the true peak, refined with a parabola at each local maximum, with 0.05 dB kept in hand. |
| **Oversampling** | 1x to 32x for the clipper, the main limiter and Balance. Linear phase half band FIR cascade, about 110 dB of rejection, latency padded to whole samples. |
| **Ext Sidechain** | The detectors listen to the sidechain input instead of the programme, scaled by the same Gain. The true peak stage still watches the programme, so the ceiling holds. |
| **DC Filter** | 5 Hz high pass on the way in. |
| **Gain Match** | Monitors at the input's short term loudness, so a louder master is not mistaken for a better one. The meters still read the real output. |
| **Delta** | Plays what the limiter removes: the driven input minus the output. |
| **Word Length / Dither** | 24, 20 or 16 bit, with TPDF or noise shaped dither. Last in the chain. |

### Metering

* The main display: about seven seconds of the driven input (grey), the output
  (teal) and the gain reduction hanging from the top (orange), all on one dB
  scale, with the ceiling drawn across it.
* Input and output peak bars with hold, a gain reduction bar, and the largest
  values since the editor opened.
* BS.1770 / EBU R128 loudness: integrated, short term, momentary, loudness
  range (EBU Tech 3342), true peak, and VU (300 ms, 0 VU at −18 dBFS). Click
  the panel to reset.
* **Target**: the integrated loudness against a delivery target (club, CD,
  streaming −14, Apple Music −16, podcast, EBU R128 −23, ATSC A/85 −24), green
  within 1 LU.
* Input correlation and the transient link in use, under the main display.
* The compressor's own reduction, beside its controls.

## How the Modern limiter works

The peak stage works on gain reduction in dB per channel, in four steps:

1. **Requirement** — what each sample needs to sit at the ceiling, with the
   channels linked by blending each detector towards the louder one.
2. **Hold** — the largest requirement over the lookahead window, kept by a
   monotonic queue in constant time per sample.
3. **Release** — reduction is taken on at once and given back by a one pole
   whose time constant moves continuously: up to four times slower when
   limiting is sustained, up to 60 % faster for a lone spike, and never faster
   than about two bass cycles while the low end dominates, which is where fast
   release turns into distortion.
4. **Attack** — two running means in series turn the edge into an S curve.

Because every value inside the hold window is at least the peak's requirement,
and both means fit inside that window, the gain applied to a peak can never be
less than the gain it needs. The ceiling is a proof, not a tuning.

## Measured

At 48 kHz, from `breakwater_tests`:

| | |
| --- | --- |
| Sample peak over the ceiling, every engine, style and factor, +18 dB drive | under 10⁻⁶ dB |
| True peak over the ceiling, True Peak on (16x reference measurement) | 0.06 dB worst case |
| Quiet signal through the chain, 1x | bit identical |
| Quiet signal through the chain, 2x to 32x | −99 dB residual, ±0.000002 dB |
| −20 dBFS, 997 Hz stereo | −20.0 LUFS integrated |
| Oversampling latency, 1x / 2x / 4x / 8x / 16x / 32x | 0 / 83 / 91 / 94 / 95 / 95 samples |
| −6 dBFS tone into the compressor, −20 dB at 4:1 | −10.5 dB |
| Mid/side compressor, loud side and quiet mid | side −9.0 dB, mid untouched |
| −18 dBFS sine | 0.0 VU |
| Total latency at the defaults (4x, 2 ms lookahead) | 284 samples, 5.9 ms |

The true peak stage and the final ramp add 25 + 72 samples at 48 kHz to every
setting; the Modern engine adds its lookahead.

## Where the features come from

The feature set was worked out from the public descriptions of three well
known mastering limiters (manufacturer pages and documentation, reviews, and
news coverage of their current versions). What each contributed, and where it
lives here:

| Reference feature | In Breakwater |
| --- | --- |
| Eight program dependent limiting styles | Modern engine, eight styles |
| Lookahead, attack and release | Lookahead, Attack, Release |
| Separate channel linking for transients and release | Link Trans, Link Rel |
| True peak limiting, oversampling to 32x | True Peak, 1x to 32x |
| Audition of what is removed | Delta |
| Unity gain monitoring | Gain Match (loudness matched) |
| DC offset filtering, dither with noise shaping | DC Filter, Word Length, Dither |
| External sidechain triggering | Ext Sidechain |
| EBU R128 / BS.1770 / ATSC A/85 loudness metering and targets | Loudness panel, Target |
| A compressor stage with ratio, threshold, attack, release, sidechain filtering, M/S | Compressor |
| Transient shaping, and low frequency punch | Transients, Low Punch |
| Detail enhancement | Detail |
| Balance: how lows and highs react to the limiter | Balance |
| Drive, saturation on the way into the limiter | Clip |
| Constant gain monitoring | Gain Match |
| Peak, true peak and loudness metering with history | Meters, scope, loudness panel |
| A zero lookahead engine that reacts sample by sample | Analog engine |
| A physically modelled analogue style circuit | Analog engine's diode and two capacitor sidechain |
| Fixed threshold, driven by the input | Gain into a fixed ceiling |
| A detector window tied to note periods, a quarter of C0 by default | Window |
| Correlation based stereo linking | Adaptive Link |
| Upward compression with release based makeup | Detail |
| A dedicated true peak limiter after the main processing | The true peak stage |
| VU metering | VU |

Not carried over: surround and immersive channel layouts, and preset
management beyond the host's own.

## What it is, and what it is not

It is **not** a copy or a measured model of any of those products. No binary
was disassembled and no code or measurement of another product was used. The
algorithms are this project's own, built from published DSP methods, and the
interface is an original design; no name, logo or artwork of another product
appears in it.

## Building

Requires CMake 3.22+ and a C++20 compiler.

```bash
cd plugins/breakwater
cmake -B build -DCMAKE_BUILD_TYPE=Release          # fetches JUCE 8.0.6
cmake --build build --parallel
```

On Linux, JUCE needs the usual development headers (see the Crest README).

### Offline checks and snapshot

```bash
cmake -B build -DBREAKWATER_BUILD_TESTS=ON -DBREAKWATER_BUILD_TOOLS=ON
cmake --build build --target breakwater_tests breakwater_shot --parallel
./build/breakwater_tests_artefacts/Release/breakwater_tests
./build/breakwater_shot_artefacts/Release/breakwater_shot docs/ui.png
```

The checks assert: the peak limiter core never crosses its ceiling over
200,000 samples of bursts up to +30 dB and is a pure delay below it; the
clipper is monotonic; the true peak interpolator finds a −3 dB sampled
inter-sample peak; the ceiling holds for every engine, style, oversampling
factor and ceiling; true peak mode holds against an independent 16x
measurement, and sample peak mode alone does not; the chain is transparent
below the ceiling; reported latency matches the real latency in every
configuration; block size does not change the output; output plus delta is the
driven input; loudness reads −20 LUFS for a −20 dBFS stereo tone; Transients
and Detail move the crest factor the right way; dithered output lands on the
16 bit grid; structure changes mid stream, mono and 44.1 to 192 kHz all hold
the ceiling; the compressor takes the ratio it is set to, and in M/S leaves a
quiet mid alone; Low Punch moves a kick's attack both ways; Balance shifts the
top end's level in the right direction; a C0 Window cuts bass distortion;
identical channels are fully linked under Adaptive Link; a loud sidechain key
turns a quiet programme down; a −18 dBFS sine reads 0 VU; and state survives a
save.

## Source

```
src/dsp/PeakLimiter.h    the lookahead limiter: hold, adaptive release, S curve attack
src/dsp/AnalogLimiter.h  the zero lookahead engine
src/dsp/Leveller.h       the slow first stage of the Modern engine
src/dsp/Compressor.h     the RMS compressor in front, stereo or mid/side
src/dsp/Balance.h        low and high weighting of the limiter's reduction
src/dsp/Styles.h         the eight styles
src/dsp/Enhancer.h       Transients, Low Punch and Detail
src/dsp/SoftClipper.h    the clip stage
src/dsp/TruePeak.h       8x interpolator with parabolic refinement
src/dsp/Oversampler.h    linear phase half band cascade, 1x to 32x
src/dsp/Loudness.h       BS.1770 loudness and EBU 3342 range
src/dsp/Quantizer.h      word length and dither
src/dsp/Primitives.h     sliding maximum, running mean, delay
src/dsp/Meters.h         audio thread to editor
src/PluginProcessor.*    parameters, latency, the chain
src/gui/Panel.*          knobs, selectors, toggles
src/gui/Display.*        scope, meters, loudness
src/PluginEditor.*       layout, scaled as a whole from one design size
tests/Tests.cpp          the offline checks
tools/Snapshot.cpp       offscreen editor render
```

## Licence

As for the rest of this repository; see `../../LICENSE` and `../../NOTICE.md`.
