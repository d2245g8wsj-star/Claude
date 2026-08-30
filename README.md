# VocalAI

Automatic vocal tuning and alignment for a whole session — a note-based pitch
editor in the spirit of **Melodyne**, and a guide-based time aligner in the
spirit of **VocALign**, driven by an engine that works out for itself which
tracks are vocals, which one is the lead, and what each note was meant to be.

Point it at a folder of stems. It reads every track, ignores the drums and the
bass, finds the lead vocal, pulls the doubles and harmonies onto the lead's
timing, and tunes all of them — in key, at the session's concert pitch, keeping
the vibrato, the scoops and the breath.

```bash
vocalai demo my_session      # write a synthetic session to try it on
vocalai tune my_session      # tune and align every vocal in it
```

```
key A minor (confidence 1.00), concert pitch 439.63 Hz
lead vocal: 01_Lead_Vox
01_Lead_Vox              lead         9 notes  tuned  12.7c ->  1.5c
02_Vox_Double            double       9 notes  tuned  17.4c ->  2.1c  aligned +-29ms
03_Vox_Harmony_3rd       harmony      9 notes  tuned  24.7c ->  3.1c  aligned +-31ms
04_Gtr                   instrument skipped (not a vocal)
05_Bass_DI               instrument skipped (not a vocal)
06_Drums                 instrument skipped (not a vocal)
```

It depends only on **numpy** and **scipy**. WAV reading and writing, pitch
tracking, note segmentation, key detection, pitch shifting and time warping are
all implemented here.

---

## Install

```bash
python -m venv .venv && source .venv/bin/activate
pip install -e .
```

## The idea

Two things separate this from a snap-to-nearest-semitone plugin.

**It decides what was meant, not what is nearest.** Targets are chosen by a
Viterbi decode over the whole melody. The model is generative: an intended note
sequence, drawn from a key prior and a melodic-interval prior, is performed with
a slow drift plus per-note error. Decoding that recovers the intent. A phrase
sung consistently 40 cents flat is pulled into tune without its melody being
rewritten, and a blue note that the singer meant is left where it is.

There is deliberately **no term rewarding the sung interval**. That evidence is
already in the pitch of each note, and counting it twice is what makes a decoder
invent a chromatic passing note to explain away two ordinary mistakes.

**It corrects a decomposition of the pitch curve, not the curve.** Each note's
contour is split into a slow component (where the note sits, and how it sags), a
vibrato band, and the jitter above it. Only the slow part is moved. Vibrato,
jitter, and the glides into and out of a phrase are handed back untouched —
removing those is exactly what makes tuning sound synthetic.

## How a session is processed

The order mirrors how the work is done by hand.

1. **Read every track.** pYIN-style probabilistic pitch tracking (YIN's CMNDF,
   a threshold prior over candidates, and an HMM decoded with Viterbi), then
   segmentation into notes.
2. **Decide what each track is.** A transparent weighted rubric scores each
   track on cues that separate a sung voice from a played instrument — above
   all *continuous pitch movement*: a voice slides into notes and wobbles on
   them, a plucked or struck instrument jumps between discrete pitches and holds
   them still. Consonants are the other giveaway. Every track carries the
   evidence for its verdict, so a wrong call can be seen and overridden.
3. **Take the concert pitch from the instruments.** A singer who is uniformly
   flat looks exactly like a session tuned flat; tuning the whole song to the
   singer's error is the wrong answer, so the band settles it. The estimate is a
   circular mean over the notes' offsets, shrunk toward A=440 in proportion to
   how tightly they cluster — a loosely tuned take has no meaningful reference,
   and inventing one would rename every note.
4. **Find the key** from everything that has pitch (Krumhansl–Kessler profiles
   over a duration- and loudness-weighted pitch-class histogram).
5. **Align the stacks to the lead**, then **tune** — in that order, so tuning
   operates on the audio that will actually be heard.

### Segmentation

A singer's pitch curve does not come pre-cut: it slides between notes, wobbles,
breaks for consonants, and repeats a pitch without returning to silence. Three
cues are combined — sustained pitch change, unvoiced gaps, and spectral onsets
(which catch a re-articulated repeated note that the pitch curve cannot show).

A note must then have somewhere it *settles*. A segment whose centre sweeps
steadily through a large interval and never holds still is the glide of a leap,
not a note; it is dissolved rather than reported. Without that rule a segmenter
invents pitches the singer never sang — an octave leap "passes through" the
notes in between.

Each note's stable **core** is found by trimming the approach and release
glides. Only the core may be moved; the glides are what make it sound performed.

### Rendering

Pitch shifting is **TD-PSOLA**, which changes the spacing of glottal pulses
without touching their content — so the formants that make a voice sound like a
person ride along unchanged. Unvoiced material has no pulses to re-space and is
copied verbatim, which is what keeps consonants crisp instead of smeared.

Pitch marks are placed by **locking to the tracked F0's phase**, not by peak
picking. A fixed passband cannot isolate the fundamental of a voice that ranges
over an octave; on higher notes the second harmonic leaks in, the search
alternates between two peaks per cycle, and the marks come out spaced 2T apart
half the time — which renders that note an octave down. Heterodyning by the F0
track and reading the residual phase yields one mark per cycle by construction,
at any pitch.

### Alignment

MFCC, delta and onset features are matched with a multi-resolution
**band-constrained DTW**. The step pattern `{(1,1), (1,2), (2,1)}` bounds the
local slope to [1/2, 2] and, conveniently, removes the within-row dependency, so
each row is one vectorised operation instead of a Python loop over columns.

The raw path is treated with suspicion: what gets rendered is a smoothed,
slope-limited, monotone version of it, blended toward the original timing by a
strength control. Rendering is **transient-aware WSOLA** — a generous
similarity search in steady regions, clamped at consonants, because freedom to
slide 10 ms in search of a better waveform match is freedom to put the transient
in the wrong place. The search is also biased toward the requested position, so
that over silence, where every candidate scores identically, it cannot drift to
an arbitrary edge and stay there.

## Command line

```bash
vocalai tune SESSION [-o OUT]        # tune and align every vocal in a session
vocalai analyse TRACK                # report notes, key and tuning; write no audio
vocalai align DUB --guide LEAD -o F  # stand-alone time alignment
vocalai notes TRACK -o notes.json    # export the editable note model
vocalai render TRACK --notes F -o G  # re-render from an edited note model
vocalai demo DIR                     # write a synthetic session to try it on
```

Useful flags for `tune`:

| flag | what it does |
| --- | --- |
| `--style transparent\|natural\|tight\|hard` | how aggressive the correction is (default `natural`) |
| `--strength 0..1` | fraction of the error removed |
| `--vibrato 0..1` | `1.0` keeps vibrato, `0` flattens it |
| `--drift 0..1` | how much in-note pitch sag survives |
| `--key "F# minor"` | force a key (`chromatic` disables the scale prior) |
| `--tuning 442` | force concert pitch |
| `--no-align` / `--no-tune` | run one half of the job only |
| `--align-strength 0..1` | how far stacks are pulled onto the lead |
| `--include` / `--exclude` | filter tracks by name |
| `--dry-run` | analyse and report without writing audio |

### Styles

| style | strength | vibrato | what it is for |
| --- | --- | --- | --- |
| `transparent` | 0.68 | kept | fixing the worst of it, inaudibly |
| `natural` | 0.88 | kept | the default: in tune, still sung |
| `tight` | 0.96 | mostly kept | modern pop lead |
| `hard` | 1.00 | removed | the deliberate auto-tune effect |

## Python API

```python
from vocalai import process_session, SessionConfig, write_reports

result = process_session("session/", "session/tuned/", SessionConfig.from_style("tight"))
print(result.summary())
write_reports(result, "session/tuned/")
```

One track at a time:

```python
from vocalai import analyze_track, tune_track, TuneConfig
from vocalai.audio.io import read_wav, write_wav

analysis = analyze_track(read_wav("lead.wav"), name="lead")
audio, result = tune_track(analysis, TuneConfig.style("natural"), key="F# minor")
write_wav("lead_tuned.wav", audio, analysis.sample_rate)

for note in analysis.notes:
    print(note.name, f"{note.cents_off:+.0f}c", "->", note.target_name)
```

## Editing by hand

The automatic decision is a proposal, not a verdict. Export the note model,
edit it, and re-render:

```bash
vocalai notes lead.wav -o notes.json
# edit notes.json ...
vocalai render lead.wav --notes notes.json -o lead_edited.wav
```

Per-note fields you can change:

| field | effect |
| --- | --- |
| `target_midi` | the note it should become |
| `pitch_offset_cents` | nudge on top of the target |
| `strength` | `1.0` moves it exactly, `0` leaves it alone |
| `vibrato_scale` / `drift_scale` | keep, exaggerate or remove the wobble and the sag |
| `locked` | the renderer does not touch this note at all |

Moves beyond a few semitones need `--max-shift` raised — the default ±320 cents
is a safety rail against the automatic decoder making a wild move.

## Reports

Every run writes `vocalai_report.html` and `vocalai_report.json`. The HTML page
is the closest thing here to Melodyne's window: each note is drawn as a blob at
the pitch it was sung, with the pitch curve running through it, a marker at the
pitch it was moved to, and the key's scale tones tinted behind. Seeing the
decisions is what makes them arguable, and an automatic tuner you cannot argue
with is one you cannot trust with a session.

## Measured behaviour

From the test suite and the evaluation scripts, on synthesised voices with known
ground truth (a Rosenberg glottal source through formant resonators, with
jitter, shimmer, vibrato, breath and consonants):

| what | result |
| --- | --- |
| Pitch tracking accuracy | 1.9 cents median error; 0.5% gross (octave) errors |
| Note count on random melodies | 19/20 phrases segmented exactly |
| Note boundary placement | 24 ms median error |
| Target decode | 18/20 phrases decoded to the intended melody |
| PSOLA shift accuracy | under 1 cent, from 110 Hz to 659 Hz, up to ±700 cents |
| Formant preservation at +700c | F1 moves 8% (naive resampling: 38%) |
| WSOLA identity warp | exact reconstruction (r > 0.9999) |
| Alignment | onset error 49 ms → 8 ms |
| Session tuning | 12–25 cents mean error → 1.5–3.1 cents |
| Vocal classification | vocals score 0.87–0.89, instruments 0.25–0.35 |
| Speed | ~2.6× realtime for analysis; a 6-track session in ~9 s |

Run them yourself:

```bash
python -m unittest discover -s tests   # 55 tests, ~30 s
```

## Limitations

Worth knowing before you trust it with a record:

- **Monophonic only.** One voice per track. It cannot separate a chord, which is
  what Melodyne's DNA does; polyphonic material is out of scope here.
- **Errors beyond a semitone are ambiguous.** A note sung 70 cents flat *is* the
  note below, slightly sharp. No algorithm resolves that from audio alone — this
  is precisely why the note model is editable.
- **A solo violin looks a lot like a voice.** It glides and it has vibrato. The
  classifier leans on consonants to tell them apart, so a legato string line
  with no attacks may score as a vocal. Use `--exclude`.
- **Key detection needs enough material.** A four-note phrase does not determine
  a key; pass `--key` when you know it, or `--key chromatic` to disable the
  scale prior entirely.
- **Heavy bleed or two voices on one track** breaks the monophonic assumption
  and, with it, the pitch tracking.
- **WAV only.** No FLAC, AIFF or MP3, and no reading of DAW session files —
  point it at a folder of stems.
- **The vocal classifier is a rubric, not a trained model.** It is hand-weighted
  and reports its evidence, which makes it debuggable and predictable, but it
  has not been fitted to a corpus of real multitracks.

## Layout

```
vocalai/
  audio/       WAV I/O, framing, filters, resampling
  analysis/    pitch tracking, note segmentation, features, key, voice detection
  process/     PSOLA/WSOLA rendering, the tuning engine, DTW alignment
  session/     scanning, orchestration, reports
  testing/     the singing-voice synthesiser used for tests and demos
  model.py     the editable note model
  pipeline.py  track-level API
  cli.py       command line
```

## Licence

MIT.
