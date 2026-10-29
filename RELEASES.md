# IndieWave Audio plugin suite: versions and releases

Six plugins live in this repository. Crest sits at the root; the others each
have their own folder under `plugins/` and their own workflow.

| Plugin | What it is | Version | Folder | Workflow | Plugin code | Bundle id |
| --- | --- | --- | --- | --- | --- | --- |
| **Crest** | Converter and clipper | 0.9.0 beta | `/` | `macos.yml` | `Crst` | `audio.indiewave.crest` |
| **Breakwater** | Mastering limiter | 0.5.0 beta | `plugins/breakwater` | `breakwater.yml` | `Brkw` | `audio.indiewave.breakwater` |
| **Beacon** | Vari-mu tube compressor | 0.5.0 beta | `plugins/beacon` | `beacon.yml` | `Bcon` | `audio.indiewave.beacon` |
| **Swell** | Two channel harmonic equaliser | 0.3.0 beta | `plugins/swell` | `swell.yml` | `Swll` | `audio.indiewave.swell` |
| **Hinge** | Two channel Baxandall shelving equaliser | 0.3.0 beta | `plugins/hinge` | `hinge.yml` | `Hnge` | `audio.indiewave.hinge` |
| **Lacquer** | Dual passive inductor equaliser | 0.3.0 beta | `plugins/lacquer` | `lacquer.yml` | `Lacq` | `audio.indiewave.lacquer` |

All six build against JUCE 8.0.6 as VST3, AU and Standalone, and share the
manufacturer code `Idwv`. The plugin code and bundle id never change once a
plugin is public: hosts use them to find the plugin in saved sessions.

## Version numbers

Each plugin has its own version, `MAJOR.MINOR.PATCH`, set in the `project()`
line of its `CMakeLists.txt`. That is the number hosts show.

- **Below 1.0 is a public beta.** Releases are marked as pre-releases and
  titled "beta", and Crest shows BETA beside its version. Sessions saved with
  a beta are expected to open in later versions, but a parameter may still
  change behaviour.
- **MINOR** goes up for anything a user would notice: a new control, a
  changed layout, different sound at the same settings.
- **PATCH** goes up for fixes that leave sound and layout as they were.
- **1.0.0** is the first release where saved sessions are guaranteed to
  sound the same in every later 1.x.

Where each plugin starts reflects how settled it is:

- **0.9** - feature complete, only polish left (Crest).
- **0.5** - feature complete, layout still moving (Breakwater, Beacon).
- **0.3** - complete and working, young and not yet used much (Swell, Hinge,
  Lacquer).

## Releasing

1. Raise the version in the plugin's `CMakeLists.txt` and add a line to its
   section below.
2. Merge to the default branch.
3. Release it, either way:
   - On GitHub, open **Actions**, pick the plugin's workflow, choose **Run
     workflow** on the default branch and tick **release**. The workflow
     tags the commit itself.
   - Or push a tag with the plugin's name in front:

     ```bash
     git tag beacon-v0.5.1 && git push origin beacon-v0.5.1
     ```

The workflow builds, runs the offline checks, refuses a tag that disagrees
with `CMakeLists.txt`, and publishes `Beacon-0.5.1-macOS.zip` on a release
named "Beacon 0.5.1 beta".

Separately, every push to the default branch replaces the rolling
`<plugin>-latest` pre-release with a fresh build. Pushes to other branches
still build and test, but do not publish, so the latest link always carries
the default branch's code.

## Changes

### Crest
- **0.9.0** - First public beta. K weighting now follows the BS.1770
  coefficient table exactly.

### Breakwater
- **0.5.0** - First public beta (was Zenith during development). Simple view
  by default with everything else behind Advanced; the editor size is
  remembered.

### Beacon
- **0.5.0** - First public beta (was Kiln during development). One set of
  controls for both channels, with separate mid and side controls in
  mid/side mode.

### Swell
- **0.3.0** - First public beta (was Ember during development; hosts list it
  as a new plugin).

### Hinge
- **0.3.0** - First public beta. Bit exact on Apple silicon.

### Lacquer
- **0.3.0** - First public beta, with the Classic White and Lacquer Black
  finishes.
