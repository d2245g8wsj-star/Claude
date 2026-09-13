# VocalAI ARA 2 plug-in

The ARA layer on top of the native core. ARA exists for plug-ins that are
closer to a sample editor than to a realtime processor, which is exactly this
engine's shape — key detection, note segmentation and the Viterbi decode over
the melody all need the whole take before they can decide anything. ARA hands
the plug-in the host's audio at will, so none of that has to be faked with a
transfer step.

```bash
./run_tests.sh        # fetches the ARA SDK, builds, runs the integration harness
```

## How it is validated without a DAW

ARA's companion APIs (VST3, AU, CLAP) only expose the `ARAFactory`. Everything
interesting behind that factory is platform independent, so the integration can
be exercised for real — model graph, analysis, content export, playback
rendering, teardown — from an in-process host, with no DAW, no companion API
wrapper and no macOS. `tests/ara_harness.cpp` is that host. ARA's own API
validation layer is left switched on, so misusing the API aborts rather than
passing quietly.

What the harness asserts (26 checks, all passing on Linux):

- the factory is published, advertises ARA 2 and note analysis
- a document controller is created and a full model graph is built — musical
  context, region sequence, audio source, audio modification, playback region
- a plug-in instance binds in the playback renderer role
- analysis runs on request, reports progress start/update/complete, and stops
  reporting itself as incomplete
- notes are exported through the ARA content API, in time order, inside the
  source, with plausible frequencies, and again through the playback region
  (shifted into song time)
- the renderer produces audio, does not blow up, contains no NaN, and renders
  silence while the transport is stopped
- every host audio reader is closed on teardown

The rendered audio is then compared against the validated CLI on the same
input: **−137 dB RMS difference**, which is float32 quantisation, with
identical tuning (22.9 cents mean error in, 2.1 cents out). Mono and stereo
sources both pass.

## The model

The division of labour follows ARA's own:

| object | owns |
| --- | --- |
| `VocalAIAudioSource` | what is true of the *recording*: samples, pitch track, notes. Expensive, and shared between modifications. |
| `VocalAIAudioModification` | what the *user decided*: tuning style, key, per-note edits, and the audio those decisions produce. Two modifications of one source can be tuned differently. |
| `VocalAIPlaybackRenderer` | nothing but reading the finished render. |

Analysis and rendering run on worker threads; completed jobs are integrated on
the model thread from `willNotifyModelUpdates`, which is where the host is
notified. Editing is locked out while a renderer is inside the model graph —
editing the graph underneath a running audio callback is the one mistake ARA
cannot protect a plug-in from.

## Two bugs the harness caught

Worth recording, because a self-consistent test would have found neither:

- the model-access counter was taken before access was granted or refused, but
  only released on the granted path. A refused render leaked the count and
  would have stalled the next edit forever.
- the harness itself never assigned the playback region to the renderer, so the
  renderer correctly produced silence. That is a *host* responsibility, and
  finding it here is the point: it is the kind of thing that otherwise shows up
  as "no sound in Logic" with no idea which side is at fault.

## What is not done

- **No companion API wrapper.** There is no `.vst3` or `.component` yet, so no
  DAW can load this. The ARA-specific part of such a wrapper is small — expose
  the factory — and the SDK's own `ARA_Examples/TestPlugIn/VST3` and
  `AudioUnit` show the shape. The rest is standard plug-in boilerplate.
- **Nothing here has been compiled or run on macOS.** AU cannot be built off
  macOS and `auval` only exists there, so that step has to happen on a Mac.
- **No persistency.** `doStoreObjectsToArchive` deliberately returns `false`
  rather than claiming success it cannot deliver: a reopened session re-runs
  analysis instead of silently losing edits it pretended to save.
- **No editor view.** The note model is editable through the core's JSON, but
  there is no UI, and no `EditorView` role.
- **No time alignment.** The VocALign half of the engine is not exposed through
  ARA yet; ARA can see other tracks in the document, so it is possible.
