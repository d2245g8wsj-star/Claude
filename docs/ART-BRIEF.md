# CREST — art brief for the next Blender pass

Written from measurements of the current handover, not impressions. Every number
below was taken off the delivered PNGs; the checks at the end are the ones the
plugin side will run before accepting the drop.

The DSP and the plugin code are finished and working. This is an art revision.

---

## 1. What is wrong with the current assets

### 1.1 The knob pointer misses the printed labels

The filmstrip sweep and the label positions on the plate do not agree.

Measured pointer angles in `character_selector_5.png` (0° = straight up,
positive = clockwise):

| Frame | 0 | 1 | 2 | 3 | 4 |
| --- | --- | --- | --- | --- | --- |
| Pointer | −128.6° | −66.8° | −5.5° | +59.1° | +141.0° |
| Gap to next | 61.8° | 61.3° | 64.6° | **81.9°** | — |

Two faults:

* **The detents are not evenly spaced.** The last gap is 82° against ~62° for
  the others. A rotary switch has equal throws between positions.
* **The sweep is not symmetric.** It runs −128.6° to +141.0°, so the whole
  travel is rotated about **+6°** clockwise. It should be −135° to +135°.

Measured label angles on `plate.png`, taken from the ink around the CHARACTER
knob centre at (514, 305) px:

| Label | Printed at | Nearest frame | Error |
| --- | --- | --- | --- |
| EDGE | −32.7° | frame 2, −5.5° | **27.2°** |
| SILK | +89.8° | frame 3, +59.1° | **30.7°** |

Half a detent out. This is the "layout klopt niet" you are seeing.

### 1.2 CHARACTER has one detent too many

The plate prints five positions: SLICE, GLASS, EDGE, SILK, TUBE. **The plugin
has four: GLASS, EDGE, SILK, TUBE.**

SLICE was removed from the DSP deliberately and should not come back. At a knee
of zero the transition region has no width, so every character already collapses
to the same square corner — SLICE was a fifth name for a setting the knee
control already reaches. Worse, because it is knee-invariant by construction,
the ADAPT control would be silently inert on it.

Right now the code maps the four characters onto frames 1–4 and never selects
frame 0, which works but leaves a dead label printed on the panel.

### 1.3 The knobs are too small, and off centre

* The knob face is **111 px of a 160 px frame — 69%**. The rest is shadow and
  highlight margin. Drawn at the diameters in `controls.json`, the visible knob
  is only ~93% of the size the layout reserves for it, which is why they read
  small.
* The face centre sits at **(82.9, 81.6)** in a 160 px frame instead of
  (80, 80): the rotation axis is **+2.9 px right and +1.6 px low** of the frame
  centre. Every knob is drawn slightly off its printed label.

### 1.4 Text baked into the plate that is not always true

`LISTENING TO A` is printed on the plate. It is only true while a blind
comparison is running. The code currently patches over it with a clean piece of
plate copied from lower down, which leaves a faint seam.

### 1.5 Art that does not exist yet

Nothing on the plate covers **oversampling, word length, dither, auto gain,
TP safe, delta, link or bypass** — eight controls. They are currently drawn by
the plugin in flat cream and amber, which does not match the rendered look.

The **IN and OUT meter windows** are not on the plate either, although they
appear in `PREVIEW_full_gui.png`. The recess is drawn in code.

---

## 2. What the next version needs

### 2.1 A bigger panel

The current 1300 × 364 pt cannot hold what is being added. Please author at:

```
plate:   2800 × 940 px     (2× for a 1400 × 470 pt editor)
```

Zone map, in editor points:

| Band | y | Contents |
| --- | --- | --- |
| Header | 0 – 72 | brand, CREST, CONVERTER · CLIPPER, corner screws |
| Controls | 80 – 250 | the twelve knobs and the character switch |
| Scope | 258 – 372 | **new wide waveform window**, x 74 – 1326 |
| Switches | 380 – 440 | oversampling, word, dither, and the five toggles |
| Footer | 444 – 470 | thin strip for the loudness readout |

The IN and OUT meter windows run vertically beside the controls band, roughly
x 26 – 62 and x 1338 – 1374, y 80 – 250.

### 2.2 The waveform window — the important new piece

Replacing the small transfer box. The plugin will draw a **scrolling waveform**
in it: the signal moves right to left, so you watch what the clipper is doing to
the material as it plays.

What the plugin will draw inside the window:

* the output waveform envelope, filled;
* behind it, in a lighter tone, the input envelope, so the difference between
  them *is* the clipping, visible as the material that was taken off the tops;
* two horizontal ceiling lines;
* the transfer curve, small, in the right-hand corner as an inset.

What the art needs to provide: **a wide recessed window with an etched
horizontal centre line and a light grid**, the same etched treatment as the
current transfer box, at x 74 – 1326, y 258 – 372. Leave a square inset panel at
the far right of it, about 110 × 110, for the transfer curve.

Do not bake any graticule labels in — the plugin draws the dB markings.

### 2.3 CHARACTER, corrected

* **Four detents, not five.** Drop SLICE.
* Sweep exactly **−135° to +135°**, so the four positions land on
  **−135°, −45°, +45°, +135°**.
* Print the labels on those four radials: GLASS at −135°, EDGE at −45°,
  SILK at +45°, TUBE at +135°.
* File: `character_selector_4.png`, four square frames stacked vertically.

Please also add the detent angles to `controls.json` so the code does not have
to infer them:

```json
"character": {
  "type": "selector",
  "positions": ["GLASS", "EDGE", "SILK", "TUBE"],
  "detent_angles_deg": [-135, -45, 45, 135]
}
```

### 2.4 Knobs, corrected and larger

* **Rotation axis exactly at the frame centre.** Currently off by (+2.9, +1.6)
  px in a 160 px frame.
* **Sweep exactly −135° to +135°**, symmetric.
* **Knob face at 82–85% of the frame width**, up from 69%. Keep enough margin
  for the shadow, but the body should fill the frame far more than it does.
* **128 frames**, 256 px square. At 48 frames the pointer moves in 5.7° steps,
  which is visible on a slow turn; 128 frames gives 2.1°.
* Two sizes would help rather than scaling one strip: `knob_large.png` for
  INPUT, CEILING, OUTPUT and `knob_small.png` for the other eight. Scaling a
  large knob down softens the knurling.

Give the small knobs more room between centres than they have now — at the
current spacing the INPUT knob overlaps the GLASS label.

### 2.5 Switch art

Needed as filmstrips so they sit in the same rendered world as the knobs:

| File | Frames | Used for |
| --- | --- | --- |
| `switch_segment.png` | 2 (off, on) | one segment of a segmented row |
| `switch_toggle.png` | 2 (off, on) | the five standalone toggles |

Segment counts the plugin needs: **oversampling 5** (OFF, 2x, 4x, 8x, 16x),
**word 4** (OFF, 24, 20, 16), **dither 3** (OFF, TPDF, SHAPED). Five toggles:
**AUTO, TP SAFE, DELTA, LINK, BYPASS**.

Bake the group headings (OVERSAMPLING, WORD, DITHER) and the toggle legends into
the plate; the plugin only draws the segment values, which change.

Add their positions to `controls.json` as rectangles in points:

```json
"switches": {
  "oversampling": { "x": 96, "y": 396, "w": 210, "h": 22, "segments": 5 },
  "...": {}
}
```

### 2.6 Remove from the plate

* `LISTENING TO A` — leave that area blank plate. The plugin writes the real
  state there.
* The SLICE label.

### 2.7 Bake into the plate

* The IN and OUT meter recesses, as dark inset windows.
* The waveform window and its inset.

---

## 3. Resolution

The editor will be resizable **from 100% to 200% in steps**. At 200% the plate
is drawn 1:1 against a 2× asset, so **2800 × 940 px is the minimum** and is
enough for a standard display. If it is cheap to render, 4× (5600 × 1880) would
also cover a 200% window on a retina screen; if not, 2× is acceptable and the
plate is a soft brushed texture that tolerates it.

Knob strips at 256 px are enough for a 75 pt knob at 200% (150 px).

---

## 4. Acceptance checks

These will be run against the drop before it is wired in:

1. Every filmstrip: frame count = image height ÷ image width, exactly.
2. Every filmstrip: pointer angle at frame 0 is −135.0° ± 0.5, at the last frame
   +135.0° ± 0.5, and the step between consecutive frames is constant ± 0.3°.
3. Every filmstrip: the knob face centroid is within 1 px of the frame centre.
4. Knob face diameter ≥ 82% of frame width.
5. `character_selector_4.png` has exactly 4 frames at −135, −45, +45, +135.
6. Label ink around the character knob falls within 3° of its detent radial.
7. `controls.json` carries `detent_angles_deg` for every selector and a
   `switches` block with rectangles for all eight switched controls.
8. Nothing in the plate reads `LISTENING TO A` or `SLICE`.

---

## 5. What does not need to change

The palette, the material, the wear and the typography are right. The brushed
plate, the machined knobs and the vintage LEDs all read the way they should.
This is a geometry and completeness pass, not a redesign.
