"""
Nazaré panel: the Blender scene and every render the editor uses.

Runs with Blender's Python module (pip install bpy==5.3.0) or inside Blender:

    python build.py                    # everything, final quality
    python build.py --only plate_front --preview

The plate is rendered without its moving parts. Each moving part (knobs,
selector, toggle, jewel, trims) is rendered on its own, frame by frame,
over the plate as a shadow catcher, so its shadow comes with it and the
lighting never rotates with the knob. All positions come from layout.json.
"""
import bpy, bmesh, json, math, os, sys
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
L = json.load(open(os.path.join(HERE, "layout.json")))
TEX = os.path.join(HERE, "textures")
OUT = os.path.join(HERE, "renders", "frames")
W, H = L["size"]
SCALE = 2  # renders are 2x the panel's point size

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
PREVIEW = "--preview" in args
ONLY = args[args.index("--only") + 1].split(",") if "--only" in args else None


# --------------------------------------------------------------------------- units
def wx(px): return (px - W / 2) / 100.0
def wy(py): return (H / 2 - py) / 100.0
def wu(px): return px / 100.0

def lin(hexstr):
    h = hexstr.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c) + (1.0,)


# --------------------------------------------------------------------------- scene
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

# world: a soft studio dome, brighter overhead and to the upper left
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

def sun(name, direction, energy, angle, color="#fff4ea"):
    d = bpy.data.lights.new(name, "SUN")
    d.energy, d.angle, d.color = energy, math.radians(angle), lin(color)[:3]
    o = bpy.data.objects.new(name, d)
    o.rotation_euler = Vector(direction).normalized().to_track_quat("-Z", "Y").to_euler()
    sc.collection.objects.link(o)
    return o

sun("key", (0.42, -0.62, -0.66), 3.6, 9)
sun("fill", (-0.7, 0.15, -0.7), 0.7, 40, "#e8eeff")
sun("top", (0.0, 0.0, -1.0), 0.6, 60)


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

def printed(name, base_hex, ink_hex, mask_path, mapping, base_rough=0.3, ink_rough=0.5, coat=0.8, metal=0.0, aniso=0.0, ink_metal=0.0):
    """A surface with a silkscreen print. mapping: 'panel' (world XY over the whole frame) or 'generated' (object bbox)."""
    m = bpy.data.materials.new(name); m.use_nodes = True
    nt = m.node_tree; nodes = nt.nodes; nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    base = nodes.new("ShaderNodeBsdfPrincipled")
    base.inputs["Base Color"].default_value = lin(base_hex); base.inputs["Metallic"].default_value = metal
    base.inputs["Coat Weight"].default_value = coat; base.inputs["Coat Roughness"].default_value = 0.06
    base.inputs["Anisotropic"].default_value = aniso
    ink = nodes.new("ShaderNodeBsdfPrincipled")
    ink.inputs["Base Color"].default_value = lin(ink_hex); ink.inputs["Roughness"].default_value = ink_rough
    ink.inputs["Metallic"].default_value = ink_metal
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

M = {}
M["lacquer"], PRINT_TEX = printed("lacquer", "#f0bdb3", "#17222f", os.path.join(TEX, "front_print.png"), "panel", base_rough=0.34)
M["nickel"], _ = principled("nickel", "#d9d8d4", rough=0.24, metal=1.0, aniso=0.75)
M["chrome"], _ = principled("chrome", "#f2f2f0", rough=0.05, metal=1.0)
M["black"], _ = principled("black", "#0c0b0b", rough=0.6)
M["gloss_black"], _ = principled("gloss_black", "#0b0b0c", rough=0.08, coat=1.0)
M["bakelite"], bk = principled("bakelite", "#efe3c8", rough=0.2, coat=0.5, coat_rough=0.07, sss=0.12)
add_noise_rough(M["bakelite"], bk, 0.16, 0.28, 18)
add_bump(M["bakelite"], bk, 300, 0.02)
M["navy"], _ = principled("navy", "#1f2d3d", rough=0.35, coat=0.4)
M["brass"], br = principled("brass", "#d4b06a", rough=0.22, metal=1.0)
add_noise_rough(M["brass"], br, 0.15, 0.32, 80)
M["collar"], _ = principled("collar", "#2a2726", rough=0.45)
M["glass"], gl = principled("glass", "#ffffff", rough=0.0, transm=1.0, ior=1.45)
M["jewel"], JEWEL = principled("jewel", "#d01d12", rough=0.03, transm=1.0, ior=1.62)
JEWEL.inputs["Emission Color"].default_value = lin("#ff3a1c")
M["enamel"], _ = printed("enamel", "#f4ead2", "#1f2d3d", os.path.join(TEX, "badge_print.png"), "generated", base_rough=0.12, ink_rough=0.3, coat=1.0)
M["alu"], _ = printed("alu", "#cfd2d4", "#2c3136", os.path.join(TEX, "serial_print.png"), "generated", base_rough=0.32, ink_rough=0.6, coat=0.0, metal=1.0, aniso=0.6)

# VU face: printed card with a little warm backlight
vu_m = bpy.data.materials.new("vu_face"); vu_m.use_nodes = True
vnt = vu_m.node_tree; vp = vnt.nodes["Principled BSDF"]
vt = image_node(vnt, os.path.join(TEX, "vu_face.png"), non_color=False)
vco = vnt.nodes.new("ShaderNodeTexCoord"); vnt.links.new(vco.outputs["Generated"], vt.inputs[0])
vnt.links.new(vt.outputs["Color"], vp.inputs["Base Color"]); vnt.links.new(vt.outputs["Color"], vp.inputs["Emission Color"])
vp.inputs["Emission Strength"].default_value = 0.22; vp.inputs["Roughness"].default_value = 0.65
M["vu_face"] = vu_m


# --------------------------------------------------------------------------- geometry helpers
COLL = {}
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


# --------------------------------------------------------------------------- the fixed panel
ear_w = wu(L["ears"])
PLATE = prism("plate", rrect_pts(W / 100 - 2 * ear_w - 0.02, H / 100 - 0.02, 0.025), -0.14, 0.0, "lacquer", bevel=0.022, segs=5, group="plate")
for side in (-1, 1):
    x = side * (W / 200 - ear_w / 2)
    ear = prism(f"ear{side}", rrect_pts(ear_w, H / 100, 0.02), -0.09, -0.02, "nickel", bevel=0.012, segs=3, group="plate", loc=(x, 0, 0))
    for sy in (-1, 1):
        y = sy * wu(H / 2 - 54 - 7)
        cut = prism(f"slot{side}{sy}", rrect_pts(0.30, 0.14, 0.069), -0.5, 0.5, "black", group="cutters", loc=(x, y, 0))
        boolean_cut(ear, cut)
        head = bpy.data.meshes.new("screw"); bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=0.085)
        bmesh.ops.scale(bm, vec=(1, 1, 0.42), verts=bm.verts)
        bm.to_mesh(head); bm.free()
        so = bpy.data.objects.new("screw", head); so.location = (x + side * 0.05, y, -0.02)
        sc.collection.objects.link(so); finish(so, "chrome", group="plate")
        for ang in (45, -45):
            c = prism("x", rrect_pts(0.13, 0.022, 0.006), -0.01, 0.08, "black", group="cutters", loc=(x + side * 0.05, y, 0))
            c.rotation_euler = (0, 0, math.radians(ang)); boolean_cut(so, c)
prism("backing", rrect_pts(W / 100, H / 100, 0.01), -0.6, -0.5, "black", group="plate")

# badge: chrome rim around a domed enamel oval
B = L["front"]["badge"]
bx, by, bw, bh = wx(B["cx"]), wy(B["cy"]), wu(B["w"]), wu(B["h"])
prism("badge_back", circle_pts(1, 200, sx=bw / 2 + 0.04, sy=bh / 2 + 0.04), 0.0, 0.03, "chrome", bevel=0.02, segs=5, group="front", loc=(bx, by, 0))
tube("badge_rim", circle_pts(1, 200, sx=bw / 2 - 0.005, sy=bh / 2 - 0.005), 0.045, 0.045, "chrome", group="front", loc=(bx, by, 0))
prism("badge_enamel", circle_pts(1, 200, sx=bw / 2 - 0.05, sy=bh / 2 - 0.05), 0.02, 0.07, "enamel", bevel=0.03, segs=6, group="front", loc=(bx, by, 0))

# VU meter: chrome bezel, black well, printed face, glass
V = L["front"]["vu"]
vx, vy, vw, vh = wx(V["cx"]), wy(V["cy"]), wu(V["w"]), wu(V["h"])
fw, fh = wu(V["face"][0]), wu(V["face"][1])
prism("vu_flange", rrect_pts(vw, vh, 0.14), 0.0, 0.035, "chrome", bevel=0.025, segs=6, group="front", loc=(vx, vy, 0))
tube("vu_rim", rrect_pts(vw - 0.08, vh - 0.08, 0.11), 0.04, 0.06, "chrome", group="front", loc=(vx, vy, 0))
prism("vu_well", rrect_pts(fw + 0.05, fh + 0.05, 0.05), 0.0, 0.05, "black", group="front", loc=(vx, vy, 0.0))
face = prism("vu_face", rrect_pts(fw, fh, 0.04), 0.0, 0.052, "vu_face", group="front", loc=(vx, vy, 0.0))

# rear: serial plate with rivets, channel window
R = L["rear"]
S = R["serial"]
sx_, sy_ = wx(S["x"] + S["w"] / 2), wy(S["y"] + S["h"] / 2)
prism("serial", rrect_pts(wu(S["w"]), wu(S["h"]), 0.05), 0.0, 0.014, "alu", bevel=0.007, segs=3, group="rear", loc=(sx_, sy_, 0))
for dx in (-1, 1):
    for dy in (-1, 1):
        me = bpy.data.meshes.new("rivet"); bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=0.05); bmesh.ops.scale(bm, vec=(1, 1, 0.4), verts=bm.verts)
        bm.to_mesh(me); bm.free()
        o = bpy.data.objects.new("rivet", me); o.location = (sx_ + dx * (wu(S["w"]) / 2 - 0.13), sy_ + dy * (wu(S["h"]) / 2 - 0.13), 0.014)
        sc.collection.objects.link(o); finish(o, "chrome", group="rear")
C_ = R["channel"]
prism("chan_win", rrect_pts(wu(C_["w"]), wu(C_["h"]), 0.04), -0.02, 0.004, "gloss_black", group="rear", loc=(wx(C_["cx"]), wy(C_["cy"]), 0))
tube("chan_rim", rrect_pts(wu(C_["w"]) + 0.03, wu(C_["h"]) + 0.03, 0.05), 0.016, 0.01, "chrome", group="rear", loc=(wx(C_["cx"]), wy(C_["cy"]), 0))


# --------------------------------------------------------------------------- moving parts
def skirted_knob(name, cx, cy, d, insert=False):
    R0 = wu(d) / 2; g = "k_" + name
    root = empty(name, (wx(cx), wy(cy), 0), g)
    parts = []
    parts.append(prism(name + "_skirt", circle_pts(R0, 200), 0.0, R0 * 0.14, "bakelite", bevel=R0 * 0.055, segs=8, group=g))
    flutes = 28 if d > 150 else 20
    rb = R0 * 0.70
    prof = lambda t: rb * (1 - 0.028 * max(0.0, math.cos(flutes * t)) ** 6)
    top = R0 * 0.56
    parts.append(prism(name + "_body", circle_pts(rb, 360, prof), R0 * 0.12, top, "bakelite", bevel=R0 * 0.075, segs=10, group=g))
    if insert:
        parts.append(prism(name + "_insert", circle_pts(R0 * 0.24, 128), top - 0.01, top + 0.012, "chrome", bevel=0.008, segs=4, group=g))
        tube(name + "_insert_ring", circle_pts(R0 * 0.25, 128), 0.006, top + 0.004, "chrome", group=g).parent = root
    # pointer: navy inlay on the skirt and across the top of the body
    w = max(0.022, R0 * 0.045)
    parts.append(prism(name + "_ptr1", rrect_pts(w, R0 * 0.20, w / 2.2), R0 * 0.14 - 0.006, R0 * 0.14 + 0.003, "navy", group=g, loc=(0, R0 * 0.83, 0)))
    r_in = R0 * (0.30 if insert else 0.16)
    parts.append(prism(name + "_ptr2", rrect_pts(w, rb * 0.86 - r_in, w / 2.2), top - 0.006, top + 0.003, "navy", group=g, loc=(0, (r_in + rb * 0.86) / 2 - 0.01, 0)))
    parent(parts, root)
    return root

def chicken_head(name, cx, cy):
    g = "k_" + name; root = empty(name, (wx(cx), wy(cy), 0), g)
    parts = [prism(name + "_base", circle_pts(0.36, 160), 0.0, 0.09, "bakelite", bevel=0.035, segs=6, group=g)]
    r, c = 0.30, (0, -0.03)
    tip = (0, 0.60)
    pts = [tip]
    for i in range(1, 80):
        a = math.radians(150 + 240 * i / 80)
        pts.append((c[0] + r * math.cos(a), c[1] + r * math.sin(a)))
    parts.append(prism(name + "_head", pts, 0.08, 0.40, "bakelite", bevel=0.075, segs=10, group=g, angle=20))
    parts.append(prism(name + "_ptr", rrect_pts(0.034, 0.40, 0.016), 0.395, 0.405, "navy", group=g, loc=(0, 0.27, 0)))
    parent(parts, root)
    return root

def toggle(name, cx, cy):
    g = "k_" + name; root = empty(name, (wx(cx), wy(cy), 0), g)
    parts = [prism(name + "_washer", circle_pts(0.24, 96), 0.0, 0.016, "chrome", bevel=0.006, segs=3, group=g),
             prism(name + "_nut", circle_pts(0.17, 6), 0.016, 0.095, "chrome", bevel=0.012, segs=3, group=g, angle=40),
             prism(name + "_bush", circle_pts(0.095, 64), 0.09, 0.17, "chrome", bevel=0.01, segs=3, group=g)]
    pivot = empty(name + "_lever", (0, 0, 0.17), g); pivot.parent = root
    me = bpy.data.meshes.new("bat"); bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=48, radius1=0.05, radius2=0.034, depth=0.62)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 0.31)); bm.to_mesh(me); bm.free()
    lever = bpy.data.objects.new("bat", me); sc.collection.objects.link(lever); finish(lever, "chrome", group=g); lever.parent = pivot
    me = bpy.data.meshes.new("ball"); bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=0.068)
    bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 0.64)); bm.to_mesh(me); bm.free()
    ball = bpy.data.objects.new("ball", me); sc.collection.objects.link(ball); finish(ball, "chrome", group=g); ball.parent = pivot
    parent(parts, root)
    return root, pivot

def jewel(name, cx, cy):
    g = "k_" + name; root = empty(name, (wx(cx), wy(cy), 0), g)
    parts = [prism(name + "_cup", circle_pts(0.2, 96), 0.0, 0.06, "chrome", bevel=0.02, segs=5, group=g)]
    tube(name + "_ring", circle_pts(0.17, 96), 0.035, 0.07, "chrome", group=g).parent = root
    me = bpy.data.meshes.new("gem"); bm = bmesh.new(); bmesh.ops.create_icosphere(bm, subdivisions=2, radius=0.14)
    bmesh.ops.scale(bm, vec=(1, 1, 0.8), verts=bm.verts); bmesh.ops.translate(bm, verts=bm.verts, vec=(0, 0, 0.04)); bm.to_mesh(me); bm.free()
    gem = bpy.data.objects.new("gem", me); sc.collection.objects.link(gem); finish(gem, "jewel", group=g, smooth=False); gem.parent = root
    lamp = bpy.data.lights.new("jewel_lamp", "POINT"); lamp.energy = 0.0; lamp.color = (1, 0.25, 0.1); lamp.shadow_soft_size = 0.05
    lo = bpy.data.objects.new("jewel_lamp", lamp); lo.location = (0, 0, 0.03); coll(g).objects.link(lo); lo.parent = root
    parent(parts, root)
    return root, lo

def trim(name, cx, cy, d):
    g = "k_" + name; R0 = wu(d) / 2; root = empty(name, (wx(cx), wy(cy), 0), g)
    parts = [prism(name + "_collar", circle_pts(R0, 128), 0.0, 0.05, "collar", bevel=0.015, segs=4, group=g)]
    head = prism(name + "_head", circle_pts(R0 * 0.66, 128), 0.04, 0.12, "brass", bevel=0.02, segs=5, group=g)
    slot = prism(name + "_slot", rrect_pts(R0 * 1.6, 0.045, 0.01), 0.09, 0.3, "black", group="cutters")
    slot.parent = root; boolean_cut(head, slot)
    parts.append(head)
    parent(parts, root)
    return root

F = L["front"]
KN = {
    "drive": skirted_knob("drive", F["drive"]["cx"], F["drive"]["cy"], F["drive"]["d"], insert=True),
    "small": skirted_knob("small", F["spread"]["cx"], F["spread"]["cy"], F["spread"]["d"]),
    "console": chicken_head("console", F["console"]["cx"], F["console"]["cy"]),
    "trim": trim("trim", R["age"]["cx"], R["age"]["cy"], R["age"]["d"]),
}
TOG, TOG_LEVER = toggle("toggle", F["power"]["cx"], F["power"]["cy"])
JEW, JEW_LAMP = jewel("jewel", F["jewel"]["cx"], F["jewel"]["cy"])


# --------------------------------------------------------------------------- render passes
os.makedirs(OUT, exist_ok=True)
SAMPLES = 24 if PREVIEW else 128
STRIP_SAMPLES = 16 if PREVIEW else 64

def show(groups, catcher):
    for name, c in COLL.items():
        on = name in groups
        c.hide_render = not on
    PLATE.is_shadow_catcher = catcher
    for o in COLL["plate"].objects:
        if o is not PLATE: o.is_shadow_catcher = catcher
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
    print("wrote", os.path.relpath(path, HERE), flush=True)

def want(key): return ONLY is None or key in ONLY

if want("plate_front"):
    PRINT_TEX.image = bpy.data.images.load(os.path.join(TEX, "front_print.png"), check_existing=True)
    PRINT_TEX.image.colorspace_settings.name = "Non-Color"
    show({"plate", "front", "static"}, False); full()
    render(os.path.join(OUT, "plate_front.png"), SAMPLES)

if want("plate_rear"):
    PRINT_TEX.image = bpy.data.images.load(os.path.join(TEX, "rear_print.png"), check_existing=True)
    PRINT_TEX.image.colorspace_settings.name = "Non-Color"
    show({"plate", "rear", "static"}, False); full()
    render(os.path.join(OUT, "plate_rear.png"), SAMPLES)

def strip(key, root, cx, cy, half, angles, setter=None):
    if not want(key): return
    show({"plate", "k_" + root.name}, True)
    crop(cx, cy, half)
    for i, a in enumerate(angles):
        if setter: setter(a)
        else: root.rotation_euler = (0, 0, math.radians(-a))
        render(os.path.join(OUT, f"{key}_{i:03d}.png"), STRIP_SAMPLES)
    root.rotation_euler = (0, 0, 0)

def sweep(n): return [-135 + 270 * i / (n - 1) for i in range(n)]
dr, sp = F["drive"], F["spread"]
strip("drive", KN["drive"], dr["cx"], dr["cy"], dr["d"] / 2 + 34, sweep(dr["frames"]))
strip("small", KN["small"], sp["cx"], sp["cy"], sp["d"] / 2 + 24, sweep(sp["frames"]))
co = F["console"]
strip("console", KN["console"], co["cx"], co["cy"], 92, co["angles"])
strip("trim", KN["trim"], R["age"]["cx"], R["age"]["cy"], R["age"]["d"] / 2 + 14, sweep(L["trimFrames"]))
pw = F["power"]
def lever(a): TOG_LEVER.rotation_euler = (math.radians(a), 0, 0)
strip("toggle", TOG, pw["cx"], pw["cy"], 58, [-38, 38], lever)
je = F["jewel"]
def lamp(on):
    JEW_LAMP.data.energy = 0.6 if on else 0.0
    JEWEL.inputs["Emission Strength"].default_value = 2.5 if on else 0.0
strip("jewel", JEW, je["cx"], je["cy"], 30, [0, 1], lamp)
print("done", flush=True)
