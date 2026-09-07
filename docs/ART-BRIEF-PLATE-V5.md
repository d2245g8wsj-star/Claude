# CREST — plate brief for the next Blender pass

Written from measurements of the plate currently in the repository
(`resources/gui/plate.png`, 2800 × 940 px = 1400 × 470 pt, so **pt = px / 2**).
The knobs, the selector and the LEDs are all accepted and need no work — this is
about the faceplate only.

Two things are being painted out of the delivered render at build time by
`tools/patch_plate.py`, and three things are being drawn in code that the plate
ought to carry. When this pass lands, that script and those code paths come back
out; §5 lists exactly what to delete.

---

## 1. The subtitle text changed

The plate prints `CONVERTER · CLIPPER` at the top right. Its ink measured
px (2268, 62)–(2531, 73), i.e. **pt (1134, 31)–(1265.5, 36.5)**: cap height
5.5 pt, tracked out by roughly 1.6 pt.

It should read:

```
MASTERING GRADE HARMONIC CONVERTER  ·  CLIPPER
```

Same weight, same tracking, same colour, **right edge still at pt x 1266** so it
stays aligned with the OUT meter column. It will run left to about pt x 963,
which is clear — the `CREST` wordmark ends at pt x 773.

## 2. The transfer window moves to the lower left

The plate prints the small transfer inset at the **right hand end** of the
display window (well at px 1694–1946 × 334–586, legend at px y 308–331). That is
the wrong end: `Waveform::paint` writes its newest column at the window's right
edge and scrolls left, so an inset there hides material at the exact moment it
arrives and is clipped — which is the one thing the display exists to show.

Print it at **pt (342, 170, 120, 120)** instead, with the `TRANSFER` legend
centred above it at pt (342, 154, 120, 13), in the same amber as `WAVEFORM`.
Keep its internal grid at the 30 pt pitch it has now.

Nothing else in the window moves. For reference, the window's own grid measured
**81.3 px (40.65 pt) horizontal** pitch with the first line at px 822, and
**84 px (42 pt) vertical** with the first at px 264, plus the emphasised centre
line at px 390.

## 3. Labels the plate does not carry

`CEILING` and `OUTPUT` are printed only 26.5 pt above centres that the same
`controls.json` gives a 78 pt diameter, so a knob at the specified size covers
them. Print them **clear of the knob body** — 52 pt above centre, i.e. pt y 68
for CEILING (centre 120) and pt y 198 for OUTPUT (centre 250) — or move the
knob centres down and apart. Either is fine; the code will follow the file.

## 4. Make the surface photographic

This is the substantive ask, and it is the part only Blender can do. The current
render is lit flat and reads as a scan of a panel rather than a photograph of
one. The plugin now applies its own light — a soft key above the panel and a
vignette in the corners — which helps, but a screen-space gradient cannot
replace shading that knows the geometry.

In rough order of payoff:

1. **One key light, above and slightly in front**, with real falloff, so the top
   of the panel is measurably brighter than the bottom and the light has a
   direction the rest of the shading can agree with. Everything below depends on
   this existing.
2. **Anisotropic brushed aluminium**, brushed along X, with a specular highlight
   that actually travels across the panel as the surface curves away. The
   current finish has grain texture but no moving highlight, which is why it
   looks printed.
3. **Milled group wells, cut in geometry rather than painted.** The plugin draws
   these itself at the moment (see `drawGroupWell`), and a shaded gradient can
   only imply a chamfer. Model them: 1.5 pt chamfer, 2 pt deep, with real
   ambient occlusion pooling in the corners and along the wall the key light
   cannot reach. Their rectangles, in pt:

   | Group | x | y | w | h |
   | --- | --- | --- | --- | --- |
   | INPUT | 72 | 78 | 96 | 127 |
   | CLIP | 170 | 78 | 140 | 281 |
   | COLOUR / HARMONICS | 998 | 78 | 140 | 281 |
   | CEILING / OUTPUT | 1188 | 78 | 104 | 241 |

   Note the first one: at the delivered widths the INPUT well ran to pt x 174
   and the CLIP well began at 170, so they overlapped by 4 pt. The table above
   is the corrected set the code now uses.
4. **The display as a real aperture**: a bevelled edge with thickness, glass
   with a faint reflection of the panel in it, and the shadow the top edge casts
   onto the field below it. The plugin draws a `Glass` pane over the window for
   exactly this reason and would rather not have to.
5. **Contact shadows** where the knob bodies meet the plate. The filmstrips
   carry their own shadow, so keep these subtle — they should read as the same
   light, not a second one. The plugin deliberately draws none, to avoid
   stacking two.
6. **Wear, sparingly.** Slight darkening and a scuff ring around the four corner
   screws, a faint hand-oil sheen below the two large right hand knobs. Boutique
   hardware is handled hardware. Resist anything that reads as damage.
7. **16 bit output.** The gradients in 4 and the vignette will band at 8 bit,
   and banding is the one artefact that instantly reads as rendered.

## 5. Constraints — do not change these

* **2800 × 940 px.** The editor is a fixed 1400 × 470 pt design scaled as one
  transform; a different aspect ratio means re-laying out every coordinate.
* **`controls.json` coordinates stay as they are**, except where §3 explicitly
  moves the CEILING and OUTPUT labels. Every knob centre and diameter in the
  code is read from that file.
* Keep `WAVEFORM` where it is (ink at px 700–803 × 207–216) and keep the window
  at pt (330, 90, 650, 210).
* Keep the printed footer at pt (300, 368, 800, 22): the plugin covers it with a
  clean copy of the surface and writes the live sample rate and latency there.
  It just needs to stay a plain piece of panel.

## 6. Acceptance checks the plugin side will run

1. `tools/patch_plate.py` **deleted**, and the plate accepted with no pixel
   editing at all.
2. No ink anywhere in pt (1134, 31)–(1266, 37) other than the new subtitle, and
   its right edge within 2 pt of 1266.
3. The transfer well within 2 pt of pt (342, 170, 120, 120), and the right hand
   end of the window (pt x 846–980) clear of everything but grid.
4. `CEILING` and `OUTPUT` ink at least 40 pt above their knob centres.
5. Luma at the top edge of the panel measurably above the bottom edge — the test
   for §4.1 actually having happened.
6. Group well edges within 1 pt of the table in §4.3, and no two wells
   overlapping.

## 7. What comes out of the code when this lands

* `tools/patch_plate.py` — the whole file.
* In `src/PluginEditor.cpp`, from `Face::paint`: the `drawTrackedLabel` subtitle
  call, the `drawDisplayInset` call and the `TRANSFER` legend, and the two
  `drawPlateLabel` calls for `CEILING` and `OUTPUT`.
* In `src/gui/Panel.cpp`: `drawDisplayInset`, and `drawGroupWell` reduced to
  nothing (or kept only for the readout plates) once the wells are in the bake.
  The `Glass` pane can stay either way — glass in front of a live trace is
  cheap and the bake cannot know what the trace is doing.
* The screen-space light in `Face::paint` stays. It is doing a different job
  from §4.1: it follows the window when the editor is resized.
