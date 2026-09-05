# CREST — IndieWave Audio

A converter and clipper. Drive a signal into a ceiling it cannot cross, choose
how the corner is rounded, and optionally run the result through a converter
style output stage with word length, dither and noise shaping.

![The Crest editor](docs/ui.png)

Built with JUCE 8. Ships as **VST3** and **Standalone** on Windows, macOS and
Linux, plus **AU** on macOS.

---

## What it does

A clipper trades crest factor for level: it removes the tips of the peaks so the
whole signal can sit higher without the peaks going over. The two things that
decide whether that sounds like loudness or like damage are *where* the ceiling
sits and *how* the transfer curve turns the corner. Crest gives you both, and
shows you the curve you are actually using.

Then it moves that corner as the material changes: square on transients, where
a rounded corner smears the attack and a square one cannot be heard; rounded on
sustained material, where the opposite is true.

Around that sits the part most clippers leave out: the stage a converter would
have added anyway. A little level dependent softening on the way in, and a real
quantiser with dither on the way out.

### Signal flow

```
in ─┬──────────────────────────── dry (delay matched to the wet path) ──┐
    │                                                                   │
    └─ input trim ─ ÷ceiling ─┬─ upsample ─ colour ─ shaper ─┬─ downsample ┴─ mix ─ output trim ─ quantiser ─ out
                              │                      ▲       │
                              │            character · knee · bias
                              │                              └─ SC LPF ─┐
                              │                                  (x − shaper(x))
                              └─ stereo link: one gain from the louder channel
```

Everything between the input trim and the downsampler runs in a normalised
domain where **1.0 is the ceiling**, which is what makes the ceiling exact: the
shaper cannot produce a value above 1.0 by construction, at any setting.

---

## Controls

### Gain staging

| Control | Range | What it does |
| --- | --- | --- |
| **Input** | −12 … +18 dB | How hard the signal is driven into the ceiling. This is the amount of clipping. Skewed so the first few dB past the ceiling get the middle of the travel. |
| **Ceiling** | −24 … 0 dBFS | The level the output is not allowed to cross. |
| **Output** | −24 … +12 dB | Final trim, after the mix. |
| **Mix** | 0 … 100 % | Parallel clipping. The dry path is delay matched, so this nulls properly. |

### Character

| Control | What it does |
| --- | --- |
| **GLASS** | Sine knee. Short corner, bright. |
| **EDGE** | Cubic knee. Reaches the ceiling at a finite level, so it still clips flat. |
| **SILK** | Tanh knee. Smooth and asymptotic, closer to compression. |
| **TUBE** | Arctan knee. The slowest corner, weighted towards low order harmonics. |
| **Knee** | How far below the ceiling the corner starts, **in dB**, 0 to 24. At 0 it is a square corner for every character — there is no separate "hard clip" mode because at zero knee all four already are one. This is the corner for *sustained* material. |
| **Adapt** | How much harder a *transient* is clipped than the knee setting. See below. |
| **Bias** | Moves the positive and negative thresholds apart. This is the even harmonic control. |
| **Harmonics** | How much harmonic content is generated from the material the clipper is working on. Silent while nothing is clipping. |
| **Series** | Even ↔ odd. Even order is the octave: thickness. Odd order is the fifth: density and edge. |
| **Colour** | The analogue input stage: level dependent treble softening and a small asymmetric non‑linearity. At 100 % the softening tops out around −1.5 dB at 20 kHz. |
| **SC LPF** | Off, or 20 Hz … 800 Hz. Hands the low frequency part of the clipping back, so the bass keeps its shape while everything above the cutoff still clips. See below. |

Below the knee threshold the gain is exactly 1.0 — not approximately. Every
character keeps that guarantee, and the offline checks assert it.

### Knee

The knee is set in dB below the ceiling, and that is not cosmetic. Amplitude is
not how anyone hears level: a knee of "40 %" measured on amplitude opens the
soft region by only 3.4 dB, and the first half of that control's travel is worth
4 dB while the second half is worth 10.

What a knee does is also not what it is usually claimed to do. A wider knee
starts shaping earlier, so at low drive it produces measurably *more* harmonic
content, not less. What it removes is the cliff. A square corner does nothing at
all until the signal crosses the ceiling and then a great deal immediately,
which is what "it goes hard straight away" means.

Measured at 48 kHz, THD+N of a 217 Hz tone against drive and knee:

| Drive | HARD | 6 dB | 12 dB | 18 dB |
| --- | --- | --- | --- | --- |
| 1 dB | −140.8 | −73.4 | −44.0 | −39.4 |
| 3 dB | −141.4 | −47.5 | −36.6 | −34.2 |
| 6 dB | −38.9 | −29.3 | −27.2 | −26.7 |
| 12 dB | −12.4 | −12.8 | −13.3 | −13.6 |

Read down the HARD column: nothing, nothing, then 100 dB of distortion arrives
between 3 and 6 dB of drive. Read down the 12 dB column: a steady ramp. The
steepest rise per 3 dB of extra drive is **102.5 dB** with a square corner and
**13.9 dB** at a 12 dB knee — seven times shallower. That is the whole point of
the control, and the checks assert it.

Past about 12 dB of drive the knee stops mattering; everything is squashed
whatever the corner looks like.

### Adapt

Whether clipping distortion is audible depends far more on how long it lasts
than on how much was taken off. Three samples off the top of a snare are
inaudible even with a perfectly square corner — and a *rounded* corner there
smears the attack, which is the one thing a transient cannot afford. Fifty
milliseconds off a held bass note is the opposite: the square corner is
obvious, and a rounded one sounds like weight.

Every clipper on the market applies the same static curve to both.

Crest measures how much of the recent past it has spent clipping and moves the
knee accordingly:

* **Knee** sets the corner for sustained material, where a corner can be heard.
* **Adapt** pulls transients back towards a square one, where it cannot.

The measurement is deliberately not the length of a single overshoot. A
sustained low note only crosses the threshold briefly on each cycle, so by that
measure every bass peak would look like a transient. What separates the two is
repetition over a span of time — the bass clips on every cycle for half a
second, the snare clips once and is gone. A slow envelope on the "is it
clipping" flag tells them apart:

| | Envelope |
| --- | --- |
| Lone 3 ms burst | 0.030 |
| Bass note clipping every cycle | 0.221 |

Measured through the plugin at 48 kHz, EDGE, knee 60 %, driven 8 dB in,
comparing Adapt at 0 % against 100 %:

| | Change |
| --- | --- |
| Sustained tone | bit identical |
| Lone 3 ms transient | −25.2 dB |

That is the whole feature in two numbers: the character dialled in survives
untouched on sustained material, and the adaptation spends its entire effect
on transients.

The scope shows both curves — the bright trace is what the audio thread is
applying right now, the ghost behind it is the curve as dialled in.

### Harmonics

Two decisions separate this from a saturation stage bolted on the front.

**When.** It is scaled by the same envelope that drives Adapt, so it colours
sustained material, leaves transients alone, and produces nothing at all while
the clipper is idle. The plugin's one hard promise stays intact: below the
ceiling the signal comes out exactly as it went in, whatever these controls are
set to. The colour is a property of the clipping rather than an effect running
alongside it.

**Where in the spectrum.** Harmonics of the low and low mid are heard as weight
and body; harmonics of the top octaves are heard as fizz. The reason most
saturation controls sound cheap is that they distort the whole spectrum.
Generation here is fed from a band limited copy, so the series lands where it is
musical and nothing is added above it.

The stage removes the offset from its own output rather than leaving it to the
DC blocker downstream, so the main path keeps its phase response at any setting.
And it runs before the shaper, so the ceiling is still guaranteed by
construction — whatever it adds, the clipper is still the last word on level.

Measured at 48 kHz on a 217 Hz tone driven 8 dB into the ceiling, harmonic
level relative to the fundamental:

| Series | 2nd | 3rd |
| --- | --- | --- |
| Harmonics off | — | −20.2 dBc |
| **EVEN** | **−21.8 dBc** | −20.3 dBc |
| **ODD** | — | **−14.7 dBc** |

A symmetric clipper on a sine produces odd harmonics only, so the second
harmonic is a clean place to watch the even series arrive — and to see that the
odd setting does not touch it.

### SC LPF

Clipping a full mix flattens whatever dominates the peaks, and that is almost
always the low end. The result is intermodulation against everything above it:
the bass loses weight and the mids pump in time with it.

Rather than splitting the signal before the clipper, Crest filters what the
clipper *removes* and gives the low part of it back:

```
out = shaper(x) + LPF(x − shaper(x))
```

Mathematically this is the same as a band split, with one property that matters:
when nothing is being clipped, `x − shaper(x)` is exactly zero, so the filter
adds exactly zero — at any cutoff. Transparency below the ceiling is untouched
by this control, which the offline checks assert at four cutoffs.

**It trades against the ceiling, by design.** Handing low frequency material
back raises the peak, so with the sidechain open the sum can climb past where
the shaper would have held it. What happens then depends on TP Safe:

| | Result |
| --- | --- |
| **TP SAFE on** | The ceiling stays absolute. The sidechain gives back only what fits underneath it, so the harder you drive, the less the low end can escape. |
| **TP SAFE off** | The low end comes back in full. The shaper still limits everything it processes, but the restored low band may exceed the ceiling. |

Measured, at 48 kHz on a 59 Hz tone sharing the signal with a 2 kHz tone, hard
clip, showing how much level the bass loses:

| Drive | SC off | 120 Hz | 400 Hz |
| --- | --- | --- | --- |
| 3 dB | −0.07 dB | −0.02 dB | −0.01 dB |
| 6 dB, TP Safe on | −1.17 dB | −0.65 dB | −0.55 dB |
| 6 dB, TP Safe off | −1.16 dB | −0.28 dB | −0.02 dB |
| 12 dB, TP Safe on | −5.30 dB | −4.75 dB | −4.72 dB |
| 12 dB, TP Safe off | −5.29 dB | −0.70 dB | −0.05 dB |

The clip meter says `SC AT CEILING` when the sidechain is being limited by the
ceiling clamp, so the trade is visible rather than something to discover.

### Converter

| Control | What it does |
| --- | --- |
| **OS** | Oversampling: off, 2x, 4x, 8x, 16x. Linear phase half band FIR, with the passband loss measured and compensated at load, so switching factors does not change the level. |
| **WORD** | Output word length: off, 24, 20 or 16 bit. |
| **DITHER** | Off, TPDF, or TPDF with second order noise shaping. |

### Switches

| Switch | What it does |
| --- | --- |
| **AUTO** | Level matches the output against the input trim, for A/B without a loudness bias. |
| **TP SAFE** | Catches the overshoot the downsampling filter can ring past the ceiling. Leave it on unless you are measuring the filter. |
| **DELTA** | Monitors wet minus dry: only the material the clipper is removing. Silent when nothing is being clipped. |
| **LINK** | Derives one gain from the louder of the two channels and applies it to both, so hard panned peaks are not pulled toward the centre. |
| **MATCH** | Level matches the two sides of a comparison, so switching changes what you hear and not how loud it is. |
| **BLIND** | The bypass switch still selects a side; it stops saying which. REVEAL shows the answer for three seconds and then draws again. |
| **BYPASS** | Latency compensated, so bypassing does not shift the signal in time. |

---

### Honest comparison

Louder sounds better. Every clipper on the market lets you fool yourself with
that, and most of them make it easy: the processed side is louder by
construction, so of course it wins.

**MATCH** measures both sides continuously to BS.1770 and applies the
difference to whichever one you are monitoring, so switching changes the sound
and not the level. **BLIND** goes further: the switch still selects a side, it
just stops telling you which one is processed. REVEAL shows the answer for three
seconds, then draws again so the next comparison starts from nothing known.

The two sides are time aligned as well as level matched, so the switch compares
two sounds rather than two positions.

Measured, with the plugin trimmed 10 dB up:

| | Integrated |
| --- | --- |
| Processed | −10.30 LUFS |
| Unprocessed | −20.30 LUFS |
| Unprocessed, matched | −10.30 LUFS |

The loudness engine is ITU-R BS.1770-4 with the filter coefficients derived
from the analogue prototypes rather than copied from the 48 kHz table in the
specification, so it stays calibrated at 44.1, 96 and 192 kHz too. The checks
assert the specification's own calibration point — a 1 kHz tone at −20 dBFS
reads −20 LUFS — at all four rates.

Momentary, short term and integrated loudness are shown along the bottom of the
panel, with integrated gated in the two stages the specification requires.

## Measured behaviour

From `crest_tests`, at 48 kHz. The same numbers come out of the Linux build
and the macOS universal build, which is checked in CI on every push:

| Oversampling | Latency | Gain error below the ceiling | THD+N below the ceiling |
| --- | --- | --- | --- |
| off | 0 smp | 0.0000 dB | −153.6 dB |
| 2x | 49 smp | 0.0001 dB | −145.3 dB |
| 4x | 61 smp | 0.0001 dB | −143.4 dB |
| 8x | 65 smp | 0.0001 dB | −143.0 dB |
| 16x | 67 smp | 0.0001 dB | −142.9 dB |

The ceiling is held at every oversampling factor with the input driven 24 dB
into it, at ceilings from 0 to −12 dBFS.

---

## Downloads

There is no download link until a build has run. Two ways to get one:

**Every push builds macOS.** The `macOS` workflow builds a universal
(arm64 + x86_64) VST3, AU and standalone on every push, runs the offline
checks, and uploads an installable zip. Open the run under the repository's
**Actions** tab and download `Crest-macOS-universal` from the artifact list at
the bottom. Run artifacts expire after 90 days and can only be downloaded by
someone signed in to GitHub with access to the repository.

**Tag a version for a public release.** Pushing a `v*` tag attaches the same
zip to a GitHub release, which anyone can download without an account:

```bash
git tag v1.0.0 && git push origin v1.0.0
```

### Installing on macOS

```
Crest.vst3       ->  ~/Library/Audio/Plug-Ins/VST3/
Crest.component  ->  ~/Library/Audio/Plug-Ins/Components/
```

Logic and GarageBand cache their plugin scan, so after installing the AU:
`killall -9 AudioComponentRegistrar`, then reopen the host.

### Signing

Without signing secrets the workflow signs ad-hoc. That is enough to load the
plugin on the machine that built it, but macOS quarantines anything downloaded
from the internet and an ad-hoc signature does not clear that — whoever
installs it has to run `xattr -dr com.apple.quarantine` on the bundles first.
The workflow ships a note in the zip saying so.

To ship something that installs without that step, add three repository
secrets and the workflow signs with the hardened runtime instead:

| Secret | What it is |
| --- | --- |
| `MACOS_CERTIFICATE` | Developer ID Application certificate, exported as `.p12` and base64 encoded |
| `MACOS_CERTIFICATE_PASSWORD` | The password set when exporting the `.p12` |
| `MACOS_SIGNING_IDENTITY` | e.g. `Developer ID Application: Your Name (TEAMID)` |

That needs an Apple Developer account. Notarisation — Apple's server side
scan, which is what finally silences Gatekeeper for a downloaded plugin — is a
further step on top of signing and is not wired up yet.

## Building

Requires CMake 3.22+ and a C++20 compiler.

```bash
# JUCE is fetched automatically...
cmake -B build -DCMAKE_BUILD_TYPE=Release
# ...or point at a local checkout
cmake -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=/path/to/JUCE

cmake --build build --parallel
```

The VST3 lands in `build/Crest_artefacts/Release/VST3/Crest.vst3`.

On Linux, JUCE needs the usual development headers:

```bash
sudo apt install libx11-dev libxext-dev libxrandr-dev libxinerama-dev \
                 libxcursor-dev libfreetype-dev libfontconfig1-dev \
                 libasound2-dev libcurl4-openssl-dev
```

### Offline checks

```bash
cmake -B build -DCREST_BUILD_TESTS=ON
cmake --build build --target crest_tests --parallel
./build/crest_tests_artefacts/Release/crest_tests
```

These assert the properties that break silently: the ceiling holding at every
setting, transparency below the knee, latency matching what the plugin reports
to the host, sample aligned bypass, a silent delta below the ceiling, finite
output across sample rates and block sizes, and state surviving a save.

### Editor snapshot

Layout regressions do not fail a compile, so the editor can be painted to a PNG
without opening a window:

```bash
cmake -B build -DCREST_BUILD_TOOLS=ON
cmake --build build --target crest_shot --parallel
./build/crest_shot_artefacts/Release/crest_shot docs/ui.png 880
```

---

## Layout of the source

```
src/dsp/Shaper.h        the five transfer curves, shared by the DSP and the curve display
src/dsp/ColorStage.h    the analogue input stage and the DC blocker
src/dsp/Quantizer.h     word length, dither, noise shaping
src/dsp/Meters.h        lock free metering between the audio thread and the editor
src/PluginProcessor.*   parameters, gain staging, oversampling, the audio chain
src/dsp/ClipEnvelope.h  how long the clipper has been working, which drives Adapt
src/dsp/HarmonicStage.h the band limited harmonic generator
src/dsp/LowSplit.h      the sidechain low pass
src/gui/Panel.*         the panel: filmstrip knobs, LED meters, scope, plate switches
resources/gui/          the rendered plate and filmstrips, compiled in as binary data
src/PluginEditor.*      layout, scaled as a whole from one design size
tests/Tests.cpp         the offline checks
tools/Snapshot.cpp      offscreen editor render
```

The curve display and the audio thread call the same `Shaper`, so the picture
cannot drift away from what you are hearing.

---

## Originality

Crest is an independent implementation. The transfer curves, the colour and
quantiser stages, the gain staging, the metering and the entire interface were
written for this project. No code, DSP model, graphics, preset data or interface
element was taken from, decompiled from, or derived from any other product.

Converter emulation and clipping are categories of audio processing, not
protected designs — but a specific implementation of one is protected, so the
implementation here is its own.

## Licence

See `LICENSE`. JUCE and the VST3 SDK carry their own terms, and those terms
decide how you are allowed to distribute a build — read `NOTICE.md` before you
ship a binary.
