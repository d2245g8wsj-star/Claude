"""
The IndieWave panel in Blender: lacquered plate, nickel rack ears, an enamel badge,
cream bakelite knobs, chrome toggles. Grown out of Nazaré's build.py and driven by
a plugin's spec.json, so every plugin is lit, printed and rendered the same way.

    python panel.py plugins/tide/art                  # everything, final quality
    python panel.py plugins/tide/art --preview        # half size, few samples
    python panel.py plugins/tide/art --only plate_front,gain

Runs with Blender's Python module (pip install bpy==5.3.0). Each plate is rendered
without its moving parts; each kind of moving part is rendered on its own, frame
by frame, over the plate as a shadow catcher, so its shadow comes with it and the
light never turns with the knob. Frames land in <art>/renders/frames.
"""
import bpy, bmesh, json, math, os, sys
from mathutils import Vector

args = [a for a in (sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:])]
ART = os.path.abspath(args[0])
PREVIEW = "--preview" in args
ONLY = args[args.index("--only") + 1].split(",") if "--only" in args else None
SPEC = json.load(open(os.path.join(ART, "spec.json")))
TEX = os.path.join(ART, "textures")
OUT = os.path.join(ART, "renders", "frames")
SCALE = 2
SAMPLES = 24 if PREVIEW else 128
STRIP_SAMPLES = 16 if PREVIEW else 64

W = H = 0
M, COLL = {}, {}
sc = None


def wx(px): return (px - W / 2) / 100.0
def wy(py): return (H / 2 - py) / 100.0
def wu(px): return px / 100.0


def lin(hexstr):
    h = hexstr.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c) + (1.0,)


# --------------------------------------------------------------------------- scene
def setup(size):
    global W, H, sc
    W, H = size
    M.clear(); COLL.clear()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.use_adaptive_sampling = True
    sc.cycles.adaptive_threshold = 0.015
    sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 8
    sc.cycles.transmission_bounces = 8
    sc.render.resolution_x, sc.render.resolution_y = W * SCALE, H * SCALE
    sc.render.resolution_percentage = 50 if PREVIEW else 100
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.render.image_settings.color_depth = "8"
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.view_settings.exposure = -0.35

    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = W / 100.0
    cam = bpy.data.objects.new("cam", cam_data)
    cam.location = (0, 0, 20)
    sc.collection.objects.link(cam)
    sc.camera = cam

    # a soft studio dome, brighter overhead and to the upper left
    world = bpy.data.worlds.new("studio")
    sc.world = world
    world.use_nodes = True
    wn, wl = world.node_tree.nodes, world.node_tree.links
    wn.clear()
    tc = wn.new("ShaderNodeTexCoord")
    sep = wn.new("ShaderNodeSeparateXYZ")
    mix_dir = wn.new("ShaderNodeMath"); mix_dir.operation = "MULTIPLY_ADD"
    mix_dir.inputs[1].default_value = -0.35
    ramp = wn.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = lin("#1c1818")
    ramp.color_ramp.elements[1].position = 0.95
    ramp.color_ramp.elements[1].color = lin("#fff8f0")
    bg = wn.new("ShaderNodeBackground"); bg.inputs[1].default_value = 0.4
    wo = wn.new("ShaderNodeOutputWorld")
    wl.new(tc.outputs["Generated"], sep.inputs[0])
    wl.new(sep.outputs["X"], mix_dir.inputs[0])
    wl.new(sep.outputs["Z"], mix_dir.inputs[2])
    wl.new(mix_dir.outputs[0], ramp.inputs[0])
    wl.new(ramp.outputs[0], bg.inputs[0])
    wl.new(bg.outputs[0], wo.inputs[0])

    sun("key", (0.42, -0.62, -0.66), 3.6, 9)
    sun("fill", (-0.7, 0.15, -0.7), 0.7, 40, "#e8eeff")
    sun("top", (0.0, 0.0, -1.0), 0.6, 60)
    materials()


def sun(name, direction, energy, angle, color="#fff4ea"):
    d = bpy.data.lights.new(name, "SUN")
    d.energy, d.angle, d.color = energy, math.radians(angle), lin(color)[:3]
    o = bpy.data.objects.new(name, d)
    o.rotation_euler = Vector(direction).normalized().to_track_quat("-Z", "Y").to_euler()
    sc.collection.objects.link(o)


# --------------------------------------------------------------------------- materials
def principled(name, color, rough=0.4, metal=0.0, coat=0.0, coat_rough=0.05, aniso=0.0, transm=0.0, ior=1.5, sss=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes["Principled BSDF"]
    p.inputs["Base Color"].default_value = lin(color)
    p.inputs["Roughness"].default_value = rough
    p.inputs["Metallic"].default_value = metal
    p.inputs["Coat Weight"].default_value = coat
    p.inputs["Coat Roughness"].default_value = coat_rough
    p.inputs["Anisotropic"].default_value = aniso
    p.inputs["Transmission Weight"].default_value = transm
    p.inputs["IOR"].default_value = ior
    if sss:
        p.inputs["Subsurface Weight"].default_value = sss
        p.inputs["Subsurface Radius"].default_value = (0.4, 0.3, 0.2)
        p.inputs["Subsurface Scale"].default_value = 0.02
    return m, p


def add_noise_rough(m, p, lo, hi, scale=60.0):
    nt = m.node_tree
    n = nt.nodes.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value = scale; n.inputs["Detail"].default_value = 6
    r = nt.nodes.new("ShaderNodeMapRange"); r.inputs[3].default_value = lo; r.inputs[4].default_value = hi
    nt.links.new(n.outputs["Fac"], r.inputs[0]); nt.links.new(r.outputs[0], p.inputs["Roughness"])


def add_bump(m, p, scale, strength, socket="Normal"):
    nt = m.node_tree
    n = nt.nodes.new("ShaderNodeTexNoise"); n.inputs["Scale"].default_value = scale; n.inputs["Detail"].default_value = 3
    b = nt.nodes.new("ShaderNodeBump"); b.inputs["Strength"].default_value = strength
    nt.links.new(n.outputs["Fac"], b.inputs["Height"]); nt.links.new(b.outputs["Normal"], p.inputs[socket])


def image_node(nt, path, non_color=True):
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(path, check_existing=True)
    if non_color:
        t.image.colorspace_settings.name = "Non-Color"
    t.interpolation = "Cubic"
    t.extension = "EXTEND"
    return t


def printed(name, base_hex, ink_hex, mask_path, mapping, base_rough=0.3, ink_rough=0.5, coat=0.8, metal=0.0, aniso=0.0):
    """A surface with a silkscreen print. mapping: 'panel' (world XY over the plate) or 'generated' (object bbox)."""
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; nodes = nt.nodes; nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    base = nodes.new("ShaderNodeBsdfPrincipled")
    base.inputs["Base Color"].default_value = lin(base_hex); base.inputs["Metallic"].default_value = metal
    base.inputs["Coat Weight"].default_value = coat; base.inputs["Coat Roughness"].default_value = 0.06
    base.inputs["Anisotropic"].default_value = aniso
    ink = nodes.new("ShaderNodeBsdfPrincipled")
    ink.inputs["Base Color"].default_value = lin(ink_hex); ink.inputs["Roughness"].default_value = ink_rough
    ink.inputs["Coat Weight"].default_value = coat * 0.25; ink.inputs["Coat Roughness"].default_value = 0.12
    tco = nodes.new("ShaderNodeTexCoord")
    tex = image_node(nt, mask_path)
    if mapping == "panel":
        mp = nodes.new("ShaderNodeMapping")
        mp.inputs["Location"].default_value = (0.5, 0.5, 0)
        mp.inputs["Scale"].default_value = (100.0 / W, 100.0 / H, 1)
        nt.links.new(tco.outputs["Object"], mp.inputs["Vector"]); nt.links.new(mp.outputs[0], tex.inputs[0])
    else:
        nt.links.new(tco.outputs["Generated"], tex.inputs[0])
    mix = nodes.new("ShaderNodeMixShader")
    nt.links.new(tex.outputs["Color"], mix.inputs[0])
    nt.links.new(base.outputs[0], mix.inputs[1]); nt.links.new(ink.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], out.inputs["Surface"])
    # orange peel in the lacquer, roughness drift, and the print standing a hair proud
    nz = nodes.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = 900; nz.inputs["Detail"].default_value = 2
    bp = nodes.new("ShaderNodeBump"); bp.inputs["Strength"].default_value = 0.035
    nt.links.new(nz.outputs["Fac"], bp.inputs["Height"]); nt.links.new(bp.outputs["Normal"], base.inputs["Coat Normal"])
    nr = nodes.new("ShaderNodeTexNoise"); nr.inputs["Scale"].default_value = 35; nr.inputs["Detail"].default_value = 8
    rr = nodes.new("ShaderNodeMapRange"); rr.inputs[3].default_value = base_rough * 0.85; rr.inputs[4].default_value = base_rough * 1.25
    nt.links.new(nr.outputs["Fac"], rr.inputs[0]); nt.links.new(rr.outputs[0], base.inputs["Roughness"])
    pb = nodes.new("ShaderNodeBump"); pb.inputs["Strength"].default_value = 0.25; pb.inputs["Distance"].default_value = 0.0015
    nt.links.new(tex.outputs["Color"], pb.inputs["Height"]); nt.links.new(pb.outputs["Normal"], ink.inputs["Normal"])
    return m, tex


def materials():
    ink = SPEC["ink"]
    M["nickel"], _ = principled("nickel", "#d9d8d4", rough=0.24, metal=1.0, aniso=0.75)
    M["chrome"], _ = principled("chrome", "#f2f2f0", rough=0.05, metal=1.0)
    M["black"], _ = principled("black", "#0c0b0b", rough=0.6)
    M["gloss_black"], _ = principled("gloss_black", "#0b0b0c", rough=0.08, coat=1.0)
    M["screen"], _ = principled("screen", "#08090a", rough=0.35, coat=0.6, coat_rough=0.12)
    M["bakelite"], bk = principled("bakelite", "#efe3c8", rough=0.2, coat=0.5, coat_rough=0.07, sss=0.12)
    add_noise_rough(M["bakelite"], bk, 0.16, 0.28, 18)
    add_bump(M["bakelite"], bk, 300, 0.02)
    M["navy"], _ = principled("navy", SPEC.get("pointer", "#1f2d3d"), rough=0.35, coat=0.4)
    M["brass"], br = principled("brass", "#d4b06a", rough=0.22, metal=1.0)
    add_noise_rough(M["brass"], br, 0.15, 0.32, 80)
    M["collar"], _ = principled("collar", "#2a2726", rough=0.45)
    for name, (glass, glow) in dict(red=("#d01d12", "#ff3a1c"), green=("#23a83a", "#5dff6a"), amber=("#e08a12", "#ffb43a"),
                                    blue=("#1d6fd0", "#4ab0ff")).items():
        M["jewel_" + name], p = principled("jewel_" + name, glass, rough=0.03, transm=1.0, ior=1.62)
        p.inputs["Emission Color"].default_value = lin(glow)
        M["jewel_" + name + "_p"] = p


def lacquer(layer):
    return printed("lacquer", SPEC["lacquer"], SPEC["ink"], os.path.join(TEX, layer + ".png"), "panel", base_rough=0.34)[0]


# --------------------------------------------------------------------------- geometry helpers
def coll(name):
    if name not in COLL:
        c = bpy.data.collections.new(name); sc.collection.children.link(c); COLL[name] = c
    return COLL[name]


def finish(obj, mat, bevel=0.0, segs=4, group="static", smooth=True, angle=None):
    obj.data.materials.append(M[mat] if isinstance(mat, str) else mat)
    if bevel:
        b = obj.modifiers.new("bevel", "BEVEL"); b.width = bevel; b.segments = segs
        b.limit_method = "ANGLE"; b.angle_limit = math.radians(angle or 30); b.harden_normals = True
    if smooth:
        for poly in obj.data.polygons: poly.use_smooth = True
    for c in obj.users_collection: c.objects.unlink(obj)
    coll(group).objects.link(obj)
    return obj


def prism(name, pts, z0, z1, mat, bevel=0.0, segs=4, group="static", loc=(0, 0, 0), angle=None):
    me = bpy.data.meshes.new(name); bm = bmesh.new()
    vs = [bm.verts.new((x, y, z0)) for x, y in pts]
    f = bm.faces.new(vs)
    ext = bmesh.ops.extrude_face_region(bm, geom=[f])
    top = [e for e in ext["geom"] if isinstance(e, bmesh.types.BMVert)]
    bmesh.ops.translate(bm, verts=top, vec=(0, 0, z1 - z0))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); o.location = loc
    sc.collection.objects.link(o)
    return finish(o, mat, bevel, segs, group, angle=angle)


def circle_pts(r, n=160, rfun=None, sx=1.0, sy=1.0):
    out = []
    for i in range(n):
        t = 2 * math.pi * i / n
        rr = rfun(t) if rfun else r
        out.append((rr * math.cos(t) * sx, rr * math.sin(t) * sy))
    return out


def rrect_pts(w, h, r, n=12):
    pts = []
    for cx, cy, a0 in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def tube(name, pts, depth, z, mat, group="static", loc=(0, 0, 0)):
    cu = bpy.data.curves.new(name, "CURVE"); cu.dimensions = "3D"; cu.bevel_depth = depth; cu.bevel_resolution = 6
    sp = cu.splines.new("POLY"); sp.points.add(len(pts) - 1)
    for p, (x, y) in zip(sp.points, pts): p.co = (x, y, z, 1)
    sp.use_cyclic_u = True
    o = bpy.data.objects.new(name, cu); o.location = loc
    sc.collection.objects.link(o)
    o.data.materials.append(M[mat])
    for c in o.users_collection: c.objects.unlink(o)
    coll(group).objects.link(o)
    return o


def boolean_cut(target, cutter):
    m = target.modifiers.new("cut", "BOOLEAN"); m.operation = "DIFFERENCE"; m.object = cutter; m.solver = "EXACT"
    cutter.hide_render = True; cutter.hide_viewport = True


def empty(name, loc, group):
    e = bpy.data.objects.new(name, None); e.location = loc
    coll(group).objects.link(e)
    return e


def parent(children, p):
    for c in children: c.parent = p


def sphere(name, r, flat, loc, mat, group):
    me = bpy.data.meshes.new(name); bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=r)
    bmesh.ops.scale(bm, vec=(1, 1, flat), verts=bm.verts)
    bm.to_mesh(me); bm.free()
    o = bpy.data.objects.new(name, me); o.location = loc
    sc.collection.objects.link(o)
    return finish(o, mat, group=group)


# --------------------------------------------------------------------------- the plate and its fixed parts
def plate(face_name):
    ear_w = wu(SPEC["ears"])
    p = prism("plate", rrect_pts(W / 100 - 2 * ear_w - 0.02, H / 100 - 0.02, 0.025), -0.14, 0.0,
              lacquer("face_" + face_name), bevel=0.022, segs=5, group="plate")
    for side in (-1, 1):
        x = side * (W / 200 - ear_w / 2)
        ear = prism(f"ear{side}", rrect_pts(ear_w, H / 100, 0.02), -0.09, -0.02, SPEC.get("ear_metal", "nickel"),
                    bevel=0.012, segs=3, group="plate", loc=(x, 0, 0))
        for sy in (-1, 1):
            y = sy * wu(H / 2 - 54 - 7)
            cut = prism(f"slot{side}{sy}", rrect_pts(0.30, 0.14, 0.069), -0.5, 0.5, "black", group="cutters", loc=(x, y, 0))
            boolean_cut(ear, cut)
            so = sphere("screw", 0.085, 0.42, (x + side * 0.05, y, -0.02), "chrome", "plate")
            for ang in (45, -45):
                c = prism("x", rrect_pts(0.13, 0.022, 0.006), -0.01, 0.08, "black", group="cutters", loc=(x + side * 0.05, y, 0))
                c.rotation_euler = (0, 0, math.radians(ang)); boolean_cut(so, c)
    prism("backing", rrect_pts(W / 100, H / 100, 0.01), -0.6, -0.5, "black", group="plate")
    return p


def parts(face):
    for P in face["parts"]:
        t = P["type"]
        if t == "badge":
            bx, by, bw, bh = wx(P["cx"]), wy(P["cy"]), wu(P["w"]), wu(P["h"])
            enamel = printed("enamel_" + P["layer"], "#f4ead2", SPEC["ink"], os.path.join(TEX, P["layer"] + ".png"), "generated",
                             base_rough=0.12, ink_rough=0.3, coat=1.0)[0]
            prism("badge_back", circle_pts(1, 200, sx=bw / 2 + 0.04, sy=bh / 2 + 0.04), 0.0, 0.03, "chrome", bevel=0.02, segs=5, loc=(bx, by, 0))
            tube("badge_rim", circle_pts(1, 200, sx=bw / 2 - 0.005, sy=bh / 2 - 0.005), 0.045, 0.045, "chrome", loc=(bx, by, 0))
            prism("badge_enamel", circle_pts(1, 200, sx=bw / 2 - 0.05, sy=bh / 2 - 0.05), 0.02, 0.07, enamel, bevel=0.03, segs=6, loc=(bx, by, 0))
        elif t == "window":
            x, y = wx(P["x"] + P["w"] / 2), wy(P["y"] + P["h"] / 2)
            w, h, r = wu(P["w"]), wu(P["h"]), wu(P["r"])
            prism("win_flange", rrect_pts(w + 0.16, h + 0.16, r + 0.08), 0.0, 0.03, "chrome", bevel=0.02, segs=5, loc=(x, y, 0))
            tube("win_rim", rrect_pts(w + 0.06, h + 0.06, r + 0.03), 0.03, 0.045, "chrome", loc=(x, y, 0))
            prism("win_glass", rrect_pts(w, h, r), 0.0, 0.032, "screen", loc=(x, y, 0))
        elif t == "vu":
            vx, vy, vw, vh = wx(P["cx"]), wy(P["cy"]), wu(P["w"]), wu(P["h"])
            fw, fh = wu(P["face"][0]), wu(P["face"][1])
            m = bpy.data.materials.new("vu_face"); m.use_nodes = True
            nt = m.node_tree; vp = nt.nodes["Principled BSDF"]
            vt = image_node(nt, os.path.join(TEX, P["layer"] + ".png"), non_color=False)
            vco = nt.nodes.new("ShaderNodeTexCoord"); nt.links.new(vco.outputs["Generated"], vt.inputs[0])
            nt.links.new(vt.outputs["Color"], vp.inputs["Base Color"]); nt.links.new(vt.outputs["Color"], vp.inputs["Emission Color"])
            vp.inputs["Emission Strength"].default_value = 0.22; vp.inputs["Roughness"].default_value = 0.65
            prism("vu_flange", rrect_pts(vw, vh, 0.14), 0.0, 0.035, "chrome", bevel=0.025, segs=6, loc=(vx, vy, 0))
            tube("vu_rim", rrect_pts(vw - 0.08, vh - 0.08, 0.11), 0.04, 0.06, "chrome", loc=(vx, vy, 0))
            prism("vu_well", rrect_pts(fw + 0.05, fh + 0.05, 0.05), 0.0, 0.05, "black", loc=(vx, vy, 0.0))
            prism("vu_face", rrect_pts(fw, fh, 0.04), 0.0, 0.052, m, loc=(vx, vy, 0.0))
        elif t == "plaque":
            alu = printed("alu_" + P["layer"], "#cfd2d4", "#2c3136", os.path.join(TEX, P["layer"] + ".png"), "generated",
                          base_rough=0.32, ink_rough=0.6, coat=0.0, metal=1.0, aniso=0.6)[0]
            x, y = wx(P["x"] + P["w"] / 2), wy(P["y"] + P["h"] / 2)
            prism("plaque", rrect_pts(wu(P["w"]), wu(P["h"]), 0.05), 0.0, 0.014, alu, bevel=0.007, segs=3, loc=(x, y, 0))
            for dx in (-1, 1):
                for dy in (-1, 1):
                    sphere("rivet", 0.05, 0.4, (x + dx * (wu(P["w"]) / 2 - 0.13), y + dy * (wu(P["h"]) / 2 - 0.13), 0.014), "chrome", "static")


# --------------------------------------------------------------------------- moving parts
def skirted_knob(name, cx, cy, d, insert=False, flutes=None):
    R0 = wu(d) / 2; g = "k_" + name
    root = empty(name, (wx(cx), wy(cy), 0), g)
    ps = [prism(name + "_skirt", circle_pts(R0, 200), 0.0, R0 * 0.14, "bakelite", bevel=R0 * 0.055, segs=8, group=g)]
    flutes = flutes or (28 if d > 150 else 20)
    rb = R0 * 0.70
    prof = lambda t: rb * (1 - 0.028 * max(0.0, math.cos(flutes * t)) ** 6)
    top = R0 * 0.56
    ps.append(prism(name + "_body", circle_pts(rb, 360, prof), R0 * 0.12, top, "bakelite", bevel=R0 * 0.075, segs=10, group=g))
    if insert:
        ps.append(prism(name + "_insert", circle_pts(R0 * 0.24, 128), top - 0.01, top + 0.012, "chrome", bevel=0.008, segs=4, group=g))
        tube(name + "_insert_ring", circle_pts(R0 * 0.25, 128), 0.006, top + 0.004, "chrome", group=g).parent = root
    w = max(0.022, R0 * 0.045)
    ps.append(prism(name + "_ptr1", rrect_pts(w, R0 * 0.20, w / 2.2), R0 * 0.14 - 0.006, R0 * 0.14 + 0.003, "navy", group=g, loc=(0, R0 * 0.83, 0)))
    r_in = R0 * (0.30 if insert else 0.16)
    ps.append(prism(name + "_ptr2", rrect_pts(w, rb * 0.86 - r_in, w / 2.2), top - 0.006, top + 0.003, "navy", group=g,
                    loc=(0, (r_in + rb * 0.86) / 2 - 0.01, 0)))
    parent(ps, root)
    return root


def chicken_head(name, cx, cy, s=1.0):
    g = "k_" + name; root = empty(name, (wx(cx), wy(cy), 0), g)
    ps = [prism(name + "_base", circle_pts(0.36 * s, 160), 0.0, 0.09 * s, "bakelite", bevel=0.035 * s, segs=6, group=g)]
    r, c = 0.30 * s, (0, -0.03 * s)
    pts = [(0, 0.60 * s)]
    for i in range(1, 80):
        a = math.radians(150 + 240 * i / 80)
        pts.append((c[0] + r * math.cos(a), c[1] + r * math.sin(a)))
    ps.append(prism(name + "_head", pts, 0.08 * s, 0.40 * s, "bakelite", bevel=0.075 * s, segs=10, group=g, angle=20))
    ps.append(prism(name + "_ptr", rrect_pts(0.034 * s, 0.40 * s, 0.016 * s), 0.395 * s, 0.405 * s, "navy", group=g, loc=(0, 0.27 * s, 0)))
    parent(ps, root)
    return root


def toggle(name, cx, cy, s=1.0):
    g = "k_" + name; root = empty(name, (wx(cx), wy(cy), 0), g)
    ps = [prism(name + "_washer", circle_pts(0.24 * s, 96), 0.0, 0.016 * s, "chrome", bevel=0.006 * s, segs=3, group=g),
          prism(name + "_nut", circle_pts(0.17 * s, 6), 0.016 * s, 0.095 * s, "chrome", bevel=0.012 * s, segs=3, group=g, angle=40),
          prism(name + "_bush", circle_pts(0.095 * s, 64), 0.09 * s, 0.17 * s, "chrome", bevel=0.01 * s, segs=3, group=g)]
    pivot = empty(name + "_lever", (0, 0, 0.17 * s), g); pivot.parent = root
    me = bpy.data.meshes.new("bat"); bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=48, radius1=0.05 * s, radius2=0.034 * s, depth=0.62 * s)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 0.31 * s)); bm.to_mesh(me); bm.free()
    lever = bpy.data.objects.new("bat", me); sc.collection.objects.link(lever); finish(lever, "chrome", group=g); lever.parent = pivot
    me = bpy.data.meshes.new("ball"); bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=0.068 * s)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 0.64 * s)); bm.to_mesh(me); bm.free()
    ball = bpy.data.objects.new("ball", me); sc.collection.objects.link(ball); finish(ball, "chrome", group=g); ball.parent = pivot
    parent(ps, root)
    return root, pivot


def jewel(name, cx, cy, colour="red", s=1.0):
    g = "k_" + name; root = empty(name, (wx(cx), wy(cy), 0), g)
    ps = [prism(name + "_cup", circle_pts(0.2 * s, 96), 0.0, 0.06 * s, "chrome", bevel=0.02 * s, segs=5, group=g)]
    tube(name + "_ring", circle_pts(0.17 * s, 96), 0.035 * s, 0.07 * s, "chrome", group=g).parent = root
    me = bpy.data.meshes.new("gem"); bm = bmesh.new(); bmesh.ops.create_icosphere(bm, subdivisions=2, radius=0.14 * s)
    bmesh.ops.scale(bm, vec=(1, 1, 0.8), verts=bm.verts); bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 0.04 * s)); bm.to_mesh(me); bm.free()
    gem = bpy.data.objects.new("gem", me); sc.collection.objects.link(gem); finish(gem, "jewel_" + colour, group=g, smooth=False); gem.parent = root
    lamp = bpy.data.lights.new("jewel_lamp", "POINT"); lamp.energy = 0.0; lamp.color = lin(SPEC.get("jewel_glow", "#ff5a2c"))[:3]
    lamp.shadow_soft_size = 0.05
    lo = bpy.data.objects.new("jewel_lamp", lamp); lo.location = (0, 0, 0.03 * s); coll(g).objects.link(lo); lo.parent = root
    parent(ps, root)
    return root, lo


def trim(name, cx, cy, d):
    g = "k_" + name; R0 = wu(d) / 2; root = empty(name, (wx(cx), wy(cy), 0), g)
    ps = [prism(name + "_collar", circle_pts(R0, 128), 0.0, 0.05, "collar", bevel=0.015, segs=4, group=g)]
    head = prism(name + "_head", circle_pts(R0 * 0.66, 128), 0.04, 0.12, "brass", bevel=0.02, segs=5, group=g)
    slot = prism(name + "_slot", rrect_pts(R0 * 1.6, 0.045, 0.01), 0.09, 0.3, "black", group="cutters")
    slot.parent = root; boolean_cut(head, slot)
    ps.append(head)
    parent(ps, root)
    return root


def key(name, cx, cy, w, h):
    """A square push key with a cream cap; frame 1 is pressed in."""
    g = "k_" + name; root = empty(name, (wx(cx), wy(cy), 0), g)
    ww, hh = wu(w), wu(h)
    prism(name + "_bezel", rrect_pts(ww + 0.1, hh + 0.1, 0.05), 0.0, 0.03, "chrome", bevel=0.012, segs=3, group=g).parent = root
    prism(name + "_well", rrect_pts(ww + 0.03, hh + 0.03, 0.03), 0.0, 0.035, "black", group=g).parent = root
    cap = empty(name + "_cap", (0, 0, 0), g); cap.parent = root
    prism(name + "_capm", rrect_pts(ww, hh, 0.04), 0.03, 0.15, "bakelite", bevel=0.03, segs=6, group=g).parent = cap
    return root, cap


# --------------------------------------------------------------------------- render passes
def show(groups, catcher, plate_obj):
    for name, c in COLL.items():
        c.hide_render = name not in groups
    plate_obj.is_shadow_catcher = catcher
    for o in COLL["plate"].objects:
        if o is not plate_obj: o.is_shadow_catcher = catcher
    sc.render.film_transparent = catcher


def crop(cx, cy, half):
    sc.render.use_border = True; sc.render.use_crop_to_border = True
    sc.render.border_min_x = (cx - half) / W; sc.render.border_max_x = (cx + half) / W
    sc.render.border_min_y = 1 - (cy + half) / H; sc.render.border_max_y = 1 - (cy - half) / H


def full():
    sc.render.use_border = False; sc.render.use_crop_to_border = False


def render(path, samples):
    sc.cycles.samples = samples
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("wrote", os.path.relpath(path, ART), flush=True)


def want(key): return ONLY is None or key in ONLY


def half_size(K):
    t = K["type"]
    if t == "skirted": return K["d"] / 2 + K.get("pad", 26)
    if t == "chicken": return 92 * K.get("s", 1.0)
    if t == "toggle": return 58 * K.get("s", 1.0)
    if t == "jewel": return 30 * K.get("s", 1.0)
    if t == "trim": return K["d"] / 2 + 14
    if t == "key": return max(K["w"], K["h"]) / 2 + 22
    raise ValueError(t)


os.makedirs(OUT, exist_ok=True)

for fname, face in SPEC["faces"].items():
    if not want("plate_" + fname):
        continue
    setup(face["size"])
    P = plate(fname)
    parts(face)
    show({"plate", "static"}, False, P); full()
    render(os.path.join(OUT, f"plate_{fname}.png"), SAMPLES)

for kname, K in SPEC["kinds"].items():
    if not want(kname):
        continue
    C = next(c for c in SPEC["controls"] if c["kind"] == kname)
    setup(SPEC["faces"][C["face"]]["size"])
    P = plate(C["face"])
    cx, cy, t = C["cx"], C["cy"], K["type"]
    setter = None
    if t == "skirted":
        root = skirted_knob(kname, cx, cy, K["d"], K.get("insert", False), K.get("flutes"))
        frames = K["angles"]
    elif t == "chicken":
        root = chicken_head(kname, cx, cy, K.get("s", 1.0)); frames = K["angles"]
    elif t == "trim":
        root = trim(kname, cx, cy, K["d"]); frames = K["angles"]
    elif t == "toggle":
        root, lev = toggle(kname, cx, cy, K.get("s", 1.0))
        horizontal = K.get("horizontal", False)
        # frame 0 is off: down, or left when horizontal; frame 1 is on
        frames = [-38, 38] if horizontal else [38, -38]
        setter = (lambda a, lev=lev: setattr(lev, "rotation_euler", (0, math.radians(a), 0))) if horizontal else \
                 (lambda a, lev=lev: setattr(lev, "rotation_euler", (math.radians(a), 0, 0)))
    elif t == "jewel":
        root, lamp = jewel(kname, cx, cy, K.get("colour", "red"), K.get("s", 1.0))
        p = M["jewel_" + K.get("colour", "red") + "_p"]
        frames = [0, 1]
        def setter(on, lamp=lamp, p=p):
            lamp.data.energy = 0.6 if on else 0.0
            p.inputs["Emission Strength"].default_value = 2.5 if on else 0.0
    elif t == "key":
        root, cap = key(kname, cx, cy, K["w"], K["h"])
        frames = [0, 1]
        setter = lambda down, cap=cap: setattr(cap, "location", (0, 0, -0.05 if down else 0))
    else:
        raise ValueError(t)
    show({"plate", "k_" + root.name}, True, P)
    crop(cx, cy, half_size(K))
    for i, a in enumerate(frames):
        if setter: setter(a)
        else: root.rotation_euler = (0, 0, math.radians(-a))
        render(os.path.join(OUT, f"{kname}_{i:03d}.png"), STRIP_SAMPLES)
print("done", flush=True)
