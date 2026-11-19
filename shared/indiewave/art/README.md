# The range's rendered panels

Every IndieWave panel is rendered in Blender in the style Nazaré set: a lacquered
plate between nickel rack ears, an enamel badge with the plugin's name, cream
bakelite knobs, chrome toggles and jewels. This kit grew out of Nazaré's
`build.py`; a plugin only describes its own panel.

| File | What it does |
| --- | --- |
| `specbuild.py` | Helpers for a plugin's `art/spec.py`: faces, parts, kinds of moving part, controls, the printed legends. Writes `spec.json`. |
| `prints.html`, `gen_prints.js` | Draw the silkscreen layers from `spec.json` into `art/textures/` (Jost and Yellowtail, OFL). |
| `panel.py` | The Blender scene. Renders each plate without its moving parts, and each kind of part frame by frame over the plate as a shadow catcher. |
| `assemble.py` | Packs plates (JPEG) and filmstrips (PNG sheets) into `resources/panel/` with `panel.json`, which the editor reads through `shared/indiewave/ui/Rendered.h`. |

```bash
python3 plugins/<p>/art/spec.py
node shared/indiewave/art/gen_prints.js plugins/<p>/art
python shared/indiewave/art/panel.py plugins/<p>/art [--preview] [--only plate_front,gain]
python shared/indiewave/art/assemble.py plugins/<p>/art plugins/<p>/resources/panel
```

`panel.py` needs Blender's Python module (`pip install bpy==5.3.0`, Python 3.13).
A full render of one panel takes a few minutes on four cores.

Kinds of moving part: `skirted` (bakelite knob, any number of positions or a
sweep), `chicken` (pointer selector), `trim` (slotted brass trimmer), `toggle`
(chrome bat, vertical or horizontal), `jewel` (lamp), `key` (push key).
Fixed parts: `badge`, `window` (black glass for a display the editor draws),
`vu` (meter face; the editor draws the needles), `plaque` (serial plate).
