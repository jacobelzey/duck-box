"""Print parts for the pirate duck box (layout revision 21: one-piece tray).

Everything is modelled in the box frame (printed mm):
    X = 0 top .. 225 bottom,  Y = 0 .. 168.75 across,  Z = 0 back (door) .. 87.5 front.
Each part is then turned into its own print orientation and dropped on the bed.

usage: build_print_parts.py <unpacked original 3mf> <out_v1 dir with work_box_orig_boxframe.stl> <outdir>
"""
import os, sys, json
import numpy as np
import trimesh
from shapely.geometry import Polygon, LineString, Point, box as sbox
from shapely import affinity
from shapely.ops import unary_union
from matplotlib.textpath import TextPath
from matplotlib.font_manager import FontProperties
from matplotlib.path import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lid_front import lid_parts, top_polys

root3mf, out_v1, outdir = sys.argv[1:4]
os.makedirs(outdir, exist_ok=True)

# ------------------------------------------------------------------ measured from the stock box mesh
Y_LO, Y_HI = 9.371, 159.379
YC = 0.5 * (Y_LO + Y_HI)
Z_BACK = 5.83                 # inner face of the back wall
Z_GRV0, Z_GRV1 = 73.748, 83.754   # lid groove, back and front
DIV_TOP, DIV_BOT, SHELF_FLOOR = 156.254, 161.247, 218.754
PK_Y, PK_Z = (5.621, 162.771), (8.748, 62.504)   # insert pocket under the top opening
LEDGE_X, TOPF = 17.502, 6.246
W_LID, H_LID, T_LID = 155.62, 160.0, 8.4     # 8.4 not the stock 8.5, so the colour layers start on a whole 0.2 mm layer
LID_X0 = 159.379 - H_LID
LID_Y0 = 5.621 + (163.129 - 5.621 - W_LID) / 2
LID_Z_NOM = 74.5              # lid back face, nominal (it can sit anywhere from 73.75 to 75.25)
FRONT = 73.5                  # nothing inside the box comes further forward than this

# ------------------------------------------------------------------ parts that are bought, as listed / measured
MAG = dict(d=43.2, t=6.15, recess_d=44.5, recess_depth=3.2, screw_clear=4.5)         # measured (rubber coat included)
MAG_ROWS_X, MAG_COLS_Y = (66.0, 112.0, 190.0), (30.0, 138.75)
# Break-beam sensor, measured: block 20.38 x 10.4..10.46 x 8.61, a 5 mm bulb standing 5.24 proud of the face, its centre 5.3 from
# the tip end (read off the photo); the cable leaves the other end.  A ring round the mounting hole stands 0.85 proud of the same face.
# Second photo (11 Oct, sensor in a shelf block): the bulb stands in a raised collar that is clearly wider than the bulb, and the
# ring round the mounting hole is bigger than first drawn.  Collar and ring sizes below are generous estimates from that photo.
SEN = dict(L=20.38, W=10.46, T=8.61, dome=5.24, bulb_d=5.0, led_from_end=5.3, boss=1.0, boss_from_cable_end=5.4, boss_d=5.5,
           collar_d=8.0, collar_h=1.2)
# 3 x AA holder, measured: 69 x 48.5 x 18; the two wires leave a hole in one end, about 6.6 from a narrow side and 6.5 from a large face
HOLDER = dict(L=69.0, W=48.5, T=18.0, wire=2.5, hole_from_side=6.6, hole_from_face=6.5)
BRD = dict(L=51.91, W=25.57, pcb=1.6,                                                 # measured outline
           face_to_back=6.5, face_to_tips=15.1,                                       # estimate; measured
           glass_from_antenna_end=20.4, glass_from_ribbon_edge=14.9,                  # photo estimates
           oled=(33.05, 20.28))                                                       # measured
LED = dict(d=5.0, flange_d=5.8, L=8.6, flange_t=1.0)
DUCK = (58.4, 55.9, 50.8)

SKIN_T = 1.0


# ------------------------------------------------------------------ helpers (all solids are manifold3d objects, so every boolean stays watertight)
import manifold3d as m3


class Sol:
    def __init__(self, man):
        self.m = man

    @property
    def bounds(self):
        b = self.m.bounding_box(); return np.array([b[:3], b[3:]])

    @property
    def volume(self):
        return self.m.volume()

    @property
    def is_empty(self):
        return self.m.is_empty()

    @property
    def is_watertight(self):
        return self.m.status() == m3.Error.NoError and not self.m.is_empty()

    def copy(self):
        return Sol(self.m)

    def apply_transform(self, M):
        self.m = self.m.transform(np.asarray(M, float)[:3, :4]); return self

    def apply_translation(self, t):
        self.m = self.m.translate([float(v) for v in t]); return self

    def split(self, only_watertight=False):
        return [Sol(x) for x in self.m.decompose()]

    def tri(self):
        me = self.m.to_mesh64()
        return trimesh.Trimesh(np.asarray(me.vert_properties)[:, :3], np.asarray(me.tri_verts), process=False)

    def export(self, path):
        t = self.tri(); t.merge_vertices(); t.export(path)


def from_tri(t):
    t = t.copy(); t.merge_vertices()
    if t.volume < 0:
        t.invert()
    man = m3.Manifold(m3.Mesh64(vert_properties=np.asarray(t.vertices, np.float64), tri_verts=np.asarray(t.faces, np.uint64)))
    assert man.status() == m3.Error.NoError, man.status()
    return Sol(man)


def fix(m):
    return m


def bx(x0, x1, y0, y1, z0, z1):
    assert x1 > x0 and y1 > y0 and z1 > z0, (x0, x1, y0, y1, z0, z1)
    return Sol(m3.Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0]))


FRAMES = {
    "xy": np.array([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1.0]]),            # poly (X, Y), extrude Z
    "uv": np.array([[0, 1, 0, LID_X0], [1, 0, 0, LID_Y0], [0, 0, 1, 0], [0, 0, 0, 1.0]]),  # poly (u, v) on the lid, extrude Z
    "yz": np.array([[0, 0, 1, 0], [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1.0]]),            # poly (Y, Z), extrude X
    "xz": np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, 1, 0, 0], [0, 0, 0, 1.0]]),            # poly (X, Z), extrude Y
    "lid": np.eye(4),                                                                      # poly (u, v), extrude w (lid-local)
    "lid_vw": np.array([[0, 0, 1, 0], [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1.0]]),        # poly (v, w), extrude u (lid-local)
}


def prism(geom, a0, a1, frame="xy"):
    assert a1 > a0, (a0, a1)
    geoms = list(geom.geoms) if hasattr(geom, "geoms") else [geom]
    rings = []
    for g in geoms:
        if g.geom_type != "Polygon" or g.is_empty or g.area < 0.005:
            continue
        rings.append(np.array(g.exterior.coords)[:-1])
        rings += [np.array(r.coords)[:-1] for r in g.interiors]
    assert rings, "empty prism"
    cs = m3.CrossSection(rings, m3.FillRule.EvenOdd)
    man = m3.Manifold.extrude(cs, a1 - a0).translate([0, 0, a0]).transform(FRAMES[frame][:3, :4])
    assert man.status() == m3.Error.NoError and man.volume() > 0
    return Sol(man)


def U(*ms):
    ms = [m.m for m in ms if m is not None]
    return Sol(m3.Manifold.batch_boolean(ms, m3.OpType.Add)) if len(ms) > 1 else Sol(ms[0])


CAT = U


def D(a, *cuts):
    r = a.m
    for c in cuts:
        r = r - c.m
    return Sol(r)


def I(a, b):
    return Sol(a.m ^ b.m)


def cyl_z(d, z0, z1, x, y, sections=96):
    return Sol(m3.Manifold.cylinder(z1 - z0, d / 2, d / 2, sections).translate([x, y, z0]))


def rrect(cx, cy, w, h, r=2.5):
    r = min(r, w / 2 - 0.01, h / 2 - 0.01)
    return sbox(cx - w / 2 + r, cy - h / 2 + r, cx + w / 2 - r, cy + h / 2 - r).buffer(r, quad_segs=8)


def obround(cx, cy, w, h):
    r = min(w, h) / 2
    return LineString([(cx - (w / 2 - r), cy - (h / 2 - r)), (cx + (w / 2 - r), cy + (h / 2 - r))]).buffer(r, quad_segs=10) if (w != h) else Point(cx, cy).buffer(r, quad_segs=10)


def uv2xy(g):
    """lid (u, v) -> box (X, Y)"""
    return affinity.affine_transform(g, [0, 1, 1, 0, LID_X0, LID_Y0])


FONT = FontProperties(family="DejaVu Sans", weight="bold")


def text_geom(s, width, cu, cv):
    tp = TextPath((0, 0), s, size=10, prop=FONT)
    rings = [Polygon(p).buffer(0) for p in tp.to_polygons() if len(p) >= 4]
    g = Polygon()
    for r in sorted([r for r in rings if r.area > 1e-6], key=lambda r: -r.area):
        g = g.symmetric_difference(r)
    x0, y0, x1, y1 = g.bounds
    k = width / (x1 - x0)
    g = affinity.scale(affinity.translate(g, -(x0 + x1) / 2, -(y0 + y1) / 2), k, -k, origin=(0, 0))
    return affinity.translate(g, cu, cv)


def loft(pa, pb, wa, wb):
    a = np.array(pa.exterior.coords)[:-1]; b = np.array(pb.exterior.coords)[:-1]
    n = len(a); assert n == len(b)
    v = np.vstack([np.c_[a, np.full(n, wa)], np.c_[b, np.full(n, wb)], [[*a.mean(0), wa]], [[*b.mean(0), wb]]])
    f = []
    for i in range(n):
        j = (i + 1) % n
        f += [[i, j, n + j], [i, n + j, n + i], [2 * n, j, i], [2 * n + 1, n + i, n + j]]
    m = trimesh.Trimesh(v, f, process=True)
    if m.volume < 0:
        m.invert()
    assert m.is_watertight
    return from_tri(m)


P = {}        # name -> mesh in the box frame (assembled position)
META = {}


def solid(name, m, pieces=1):
    m = fix(m)
    assert m.is_watertight, name + " is not watertight"
    n = len(m.split(only_watertight=False))
    assert n == pieces, f"{name}: {n} separate pieces, expected {pieces}"
    P[name] = m
    return m


# ================================================================== 2D art on the lid (u across, v down)
lp = lid_parts(root3mf)
uvz, f = lp["4"]
duck = unary_union([Polygon(t).buffer(0.05) for t in top_polys(uvz, f)]).buffer(-0.05)
duck = Polygon(max(duck.geoms, key=lambda g: g.area).exterior) if hasattr(duck, "geoms") else Polygon(duck.exterior)

CX = 85.5
M_, C4 = Path.MOVETO, Path.CURVE4
top_left = [(52.5, 47.5), (60.0, 46.5), (66.5, 41.0), (70.5, 33.5), (74.5, 26.0), (80.0, 21.5), (CX, 21.5)]
bot_right = [(2 * CX - 52.5, 47.5), (2 * CX - 56.5, 54.5), (2 * CX - 62.5, 58.0), (2 * CX - 68.5, 56.0),
             (2 * CX - 74.0, 54.0), (2 * CX - 79.5, 49.5), (CX, 49.5)]
mir = lambda pts: [(2 * CX - x, y) for x, y in pts]
verts = top_left + mir(top_left)[::-1][1:] + bot_right[1:] + mir(bot_right)[::-1][1:]
hat = Polygon(Path(verts, [M_] + [C4] * (len(verts) - 1)).to_polygons()[0]).buffer(0)
top_edge = LineString(Path(top_left + mir(top_left)[::-1][1:], [M_] + [C4] * 12).to_polygons(closed_only=False)[0])

# the back-lit emblem is a separate yellow cap: the duck, less the part hidden under the hat
C = duck.difference(hat.buffer(0.5))
C = max(C.geoms, key=lambda g: g.area) if hasattr(C, "geoms") else C
C = Polygon(C.exterior).simplify(0.02)
META["emblem"] = dict(u=[round(C.bounds[0], 2), round(C.bounds[2], 2)], v=[round(C.bounds[1], 2), round(C.bounds[3], 2)], area=round(C.area, 1))

EMB = 2.6                       # how far the emblem stands proud of the lid
CAP_H = T_LID + EMB             # 11.0
CSK = 2.0                       # 45 degree countersink at the lid back, in 0.2 mm steps
STEP = 0.2

# screen porthole: centred on where the glass will be
BRD_X1 = DIV_TOP - 1.10         # board's lower long edge sits on the cartridge rail
BRD_X0 = BRD_X1 - BRD["W"]
BRD_Y0 = YC - (BRD["L"] - BRD["glass_from_antenna_end"])     # USB end (viewer's left)
BRD_Y1 = BRD_Y0 + BRD["L"]
GLASS_X = BRD_X1 - BRD["glass_from_ribbon_edge"]
PC = (YC - LID_Y0, GLASS_X - LID_X0)
WIN_MASK = (26.0, 14.5)         # window in the cartridge: a little bigger than the lit area (21.7 x 10.9), inside the dark glass
WIN_BACK, WIN_FRONT = (29.0, 17.5), (37.0, 23.5)     # flares 4 mm each side, 3 mm top and bottom
BEZ, BEZ_H = 1.2, 0.8
META["porthole"] = dict(u=round(PC[0], 2), v=round(PC[1], 2), glass_X=round(GLASS_X, 2), board_Y=[round(BRD_Y0, 2), round(BRD_Y1, 2)], board_X=[round(BRD_X0, 2), round(BRD_X1, 2)])

# ================================================================== LID (lid-local: u, v, w with w = 0 at the back)
plate = prism(sbox(0, 0, W_LID, H_LID), 0, T_LID, "lid")
PEG_POCKET = (8.75, 15.0, 21.25, 40.0, 6.25)
porthole = loft(rrect(*PC, *WIN_BACK, 2.0), rrect(*PC, *WIN_FRONT, 2.0), 0.0, T_LID)
porthole = U(porthole, prism(rrect(*PC, *WIN_BACK, 2.0), -1, 0.01, "lid"), prism(rrect(*PC, *WIN_FRONT, 2.0), T_LID - 0.01, T_LID + 1, "lid"))
hole_cuts = [prism(C.buffer(0.1), CSK - 0.01, T_LID + 1, "lid")]
for i in range(int(round(CSK / STEP))):
    hole_cuts.append(prism(C.buffer(0.1 + CSK - i * STEP), i * STEP - (1 if i == 0 else 0.001), (i + 1) * STEP, "lid"))
chamfer = prism(Polygon([(H_LID - 1.2, -0.1), (H_LID + 0.1, -0.1), (H_LID + 0.1, 1.3)]), -1, W_LID + 1, "lid_vw")      # lead-in on the bottom back edge
lid_black = D(plate, porthole, prism(sbox(*PEG_POCKET[:4]), -1.0, PEG_POCKET[4], "lid"), chamfer, *hole_cuts)

front_open = rrect(*PC, *WIN_FRONT, 2.0)
bezel = front_open.buffer(BEZ).difference(front_open.buffer(0.001))
TXT_V = 121.8
txt1 = text_geom("DROP YER DUCK", 92, W_LID / 2, 12.5)
txt2 = text_geom("TAKE YER LOOT", 92, W_LID / 2, TXT_V)
assert bezel.bounds[1] > txt2.bounds[3] + 1.5, (bezel.bounds, txt2.bounds)
frame = sbox(4, 4, W_LID - 4, H_LID - 4).difference(sbox(4.9, 4.9, W_LID - 4.9, H_LID - 4.9))
assert bezel.bounds[3] < H_LID - 4.9 - 1.0
assert txt2.bounds[1] > C.bounds[3] + CSK + 0.5, "text too close to the emblem"

SX, SY = CX, 37.2
bones = unary_union([LineString([(SX - 7.2, SY + 1.6 - 3.6 * s), (SX + 7.2, SY + 1.6 + 3.6 * s)]).buffer(1.05) for s in (1, -1)]
                    + [Point(SX + ex, SY + 1.6 + (3.6 * s if ex > 0 else -3.6 * s)).buffer(1.5) for s in (1, -1) for ex in (-7.2, 7.2)])
skull = unary_union([affinity.scale(Point(SX, SY - 0.6).buffer(1.0), 4.2, 3.8), rrect(SX, SY + 3.4, 4.6, 3.0, 0.8)])
eyes = unary_union([affinity.scale(Point(SX + ex, SY - 0.5).buffer(1.0), 1.05, 1.2) for ex in (-1.9, 1.9)]
                   + [Polygon([(SX, SY + 1.1), (SX - 0.7, SY + 2.4), (SX + 0.7, SY + 2.4)])])
arrow = np.array([(0, -7.6), (7.5, -0.2), (3.1, -0.2), (3.1, 7.6), (-3.1, 7.6), (-3.1, -0.2), (-7.5, -0.2)]) * 0.8
HAT_H = 3.4
hat_outline = hat.difference(hat.buffer(-1.1))
hat_trim = top_edge.buffer(4.0).difference(top_edge.buffer(3.2)).intersection(hat.buffer(-1.4))
lid_red = prism(hat, T_LID, T_LID + HAT_H, "lid")
white_bits = [prism(hat_outline, T_LID + HAT_H, T_LID + HAT_H + 0.6, "lid"),
              prism(bones.difference(skull.buffer(0.5)), T_LID + HAT_H, T_LID + HAT_H + 0.6, "lid"),
              prism(skull.difference(eyes), T_LID + HAT_H, T_LID + HAT_H + 1.0, "lid"),
              prism(txt1, T_LID, T_LID + 2.4, "lid"), prism(txt2, T_LID, T_LID + 2.4, "lid"),
              prism(frame, T_LID, T_LID + 0.8, "lid")]
yellow_bits = [prism(hat_trim, T_LID + HAT_H, T_LID + HAT_H + 0.4, "lid"), prism(bezel, T_LID, T_LID + BEZ_H, "lid")]
for cu in (17, W_LID - 17):
    yellow_bits.append(prism(Polygon(arrow + [cu, 13.5]), T_LID, T_LID + 2.4, "lid"))
ARROW_DX = 36.0
for cu in (W_LID / 2 - ARROW_DX, W_LID / 2 + ARROW_DX):
    yellow_bits.append(prism(Polygon(arrow * [1, -1] + [cu, PC[1]]), T_LID, T_LID + 2.4, "lid"))
assert Polygon(arrow * [1, -1] + [W_LID / 2 - ARROW_DX, PC[1]]).distance(bezel) > 3.0
lid_white = CAT(*white_bits)
lid_yellow = CAT(*yellow_bits)
LIDP = dict(lid_black=lid_black, lid_red=lid_red, lid_white=lid_white, lid_yellow=lid_yellow)
assert lid_black.is_watertight and len(lid_black.split(only_watertight=False)) == 1

# ================================================================== DUCK CAP (lid-local), pushed in from the back
strap = LineString([(70.2, 68.6), (100.6, 58.4)]).buffer(1.3)
patch = affinity.rotate(affinity.scale(Point(93.2, 61.2).buffer(1.0), 4.4, 3.8), -18)
inlay2d = unary_union([strap, patch]).intersection(C.buffer(-0.1))
INLAY_T = 0.6


def make_cap(skin_t):
    outer = C.buffer(-0.1)
    inner = C.buffer(-1.3)
    body = prism(outer, CSK, CAP_H, "lid")
    steps = [prism(C.buffer(CSK - 0.1 - i * STEP), i * STEP, (i + 1) * STEP + (0.001 if i < 9 else 0), "lid") for i in range(int(round(CSK / STEP)))]
    ex = np.array(outer.exterior.coords)
    seg = np.r_[0, np.cumsum(np.linalg.norm(np.diff(ex, axis=0), axis=1))]
    ribs = []
    for k in range(10):                                   # crush ribs so it is a push fit in the lid
        p = outer.exterior.interpolate((k + 0.5) / 10 * seg[-1])
        ribs.append(prism(Point(p.x, p.y).buffer(0.32, quad_segs=6), CSK + 0.4, T_LID - 0.4, "lid"))
    cap = U(body, *steps, *ribs)
    cap = D(cap, prism(inner, -1.0, CAP_H - skin_t, "lid"))
    inl = prism(inlay2d, CAP_H - min(INLAY_T, skin_t), CAP_H, "lid")
    black = fix(I(cap, inl))
    yellow = D(cap, inl)
    assert cap.is_watertight and len(cap.split(only_watertight=False)) == 1
    return cap, yellow, black


cap10, cap10_y, cap10_k = make_cap(1.0)
cap06, cap06_y, cap06_k = make_cap(0.6)

# ================================================================== BODY
body0_tri = trimesh.load(os.path.join(out_v1, "work_box_orig_boxframe.stl")); body0_tri.merge_vertices()
assert body0_tri.is_watertight
body0 = from_tri(body0_tri)
sec = body0_tri.section(plane_origin=[0, 0, 3.0], plane_normal=[0, 0, 1])
peg_slot = None
for e in sec.discrete:
    b0, b1 = e[:, :2].min(0), e[:, :2].max(0)
    if abs((b1 - b0)[1] - 12.5) < 0.3 and b0[1] < 20:
        peg_slot = (round(b0[0], 3), round(b1[0], 3), round(b0[1], 3), round(b1[1], 3))
assert peg_slot, "peg slot not found"
META["peg_slot_X_Y"] = peg_slot

adds = [bx(8.2, 45.6, 33.8, 60.0, 0.0, Z_BACK), bx(8.2, 45.6, 111.3, 137.5, 0.0, Z_BACK)]      # keyholes filled
LIP = dict(z0=70.0, z1=73.0, h=3.0)                                    # low ramp on the shelf floor: keeps resting ducks out of the shelf beam
adds.append(prism(Polygon([(SHELF_FLOOR + 0.05, LIP["z0"]), (SHELF_FLOOR - LIP["h"], LIP["z1"]), (SHELF_FLOOR + 0.05, LIP["z1"])]), Y_LO - 0.05, Y_HI + 0.05, "xz"))
cuts = []
for mx in MAG_ROWS_X:
    for my in MAG_COLS_Y:
        cuts.append(cyl_z(MAG["recess_d"], -1.0, MAG["recess_depth"], mx, my))
        cuts.append(cyl_z(MAG["screw_clear"], -1.0, Z_BACK + 1.0, mx, my, 48))

# The drop beam now runs across the box under the insert plate, so the back wall has no window any more.

# battery holder slides in front to back on the right, wire end first; a pocket in the back wall gives it room,
# and two deeper reliefs take the wires where they leave the holder (either way up)
BAT_Y = (107.9, 157.1)
BAT_POCKET_Z = 3.8
WIRE_Z = 1.2
cuts.append(bx(136.5, DIV_TOP, BAT_Y[0] - 0.5, BAT_Y[1] + 0.5, BAT_POCKET_Z, Z_BACK + 1))
BAT_YC = 0.5 * (107.7 + 157.1)                                         # middle of the slot between the tray walls
WIRE_YC = (BAT_YC - HOLDER["W"] / 2 + HOLDER["hole_from_side"], BAT_YC + HOLDER["W"] / 2 - HOLDER["hole_from_side"])
for wy in WIRE_YC:
    cuts.append(bx(135.0, 153.0, wy - 4.5, wy + 4.5, WIRE_Z, Z_BACK + 1))

# shelf beam: dovetail channels in both side walls, open at the front, plus a wire tunnel up into the electronics bay
SH = dict(X=190.0, mouth=13.6, flare=1.5, depth=6.5, z0=56.0, tunnel_z=59.0, tunnel_d=3.8, tunnel_in=3.2)


def side_cuts(right):
    my = (lambda y: 168.75 - y) if right else (lambda y: y)
    face = Y_LO
    x0, x1 = SH["X"] - SH["mouth"] / 2, SH["X"] + SH["mouth"] / 2
    dov = Polygon([(x0, my(face + 0.2)), (x1, my(face + 0.2)), (x1, my(face)), (x1 + SH["flare"], my(face - SH["depth"])),
                   (x0 - SH["flare"], my(face - SH["depth"])), (x0, my(face))]).buffer(0)
    out = [prism(dov, SH["z0"], 88.0, "xy")]
    cy, cz, r = face - SH["tunnel_in"], SH["tunnel_z"], SH["tunnel_d"] / 2
    ang = np.linspace(np.radians(45), np.radians(-225), 40)
    tear = Polygon([(my(cy), cz + r * np.sqrt(2))] + [(my(cy + r * np.cos(a)), cz + r * np.sin(a)) for a in ang]).buffer(0)
    top = 130.0
    out.append(prism(tear, top, x0 + 0.6, "yz"))
    # above the divider the tunnel opens into the bay as a groove
    apex = cz + r * np.sqrt(2)
    mouth = Polygon([(my(cy), cz - r), (my(face + 0.2), cz - r), (my(face + 0.2), apex + (face + 0.2 - cy)), (my(cy), apex)]).buffer(0)   # roof rises at 45 degrees, prints without support
    out.append(prism(mouth, top, 137.0, "yz"))            # short mouth right under the deck, so the wire can only come out where the tray leaves room
    return out


cuts += side_cuts(False) + side_cuts(True)
body = U(body0, *adds)
body = D(body, *cuts)
assert body.is_watertight and len(body.split(only_watertight=False)) == 1, "body"
assert np.allclose(body.bounds, body0.bounds)
for y in (46.9, 124.4):
    h, _, _ = body.tri().ray.intersects_location([[33.0, y, -5.0]], [[0, 0, 1.0]])
    assert len(h) and abs(h[:, 2].min()) < 1e-6, "keyhole still open"
body_white = fix(I(body, bx(-1, 226, -1, 170, -1, SKIN_T)))
body_black = fix(I(body, bx(-1, 226, -1, 170, SKIN_T, 90)))
solid("body", body)
META["magnet"] = dict(web_under_screw_head=round(Z_BACK - MAG["recess_depth"], 2), proud_of_back=round(MAG["t"] - MAG["recess_depth"], 2))

# ================================================================== INSERT PLATE (drops in from the top, rests on the two ledges)
PL_YC = 0.5 * (PK_Y[0] + PK_Y[1]); PL_ZC = 0.5 * (PK_Z[0] + PK_Z[1])
INS = dict(w=PK_Y[1] - PK_Y[0] - 0.6, t=7.761, hole_w=66.0, hole_d=49.5)       # 0.3 mm clear each side of the pocket
# The lid stands 0.62 mm proud of the box top, so its peg pocket spans X 14.38..39.38 while the slot in the back wall spans
# X 15.00..40.00.  The 23.75 mm peg therefore sits with its top somewhere between X 15.0 and 15.63, tilted a little.
PEG_X = (peg_slot[0], peg_slot[0] + 23.75 + 0.63)
IX1 = 16.2                                    # underside of the plate
IX0_OLD = IX1 - INS["t"]                     # where the top was up to v12: 8.44 below the box top, so it sat in a recess
IX0 = 0.0                                     # 13 Oct: top face flush with the top of the box (box top is X = 0)
IY0, IY1 = PL_YC - INS["w"] / 2, PL_YC + INS["w"] / 2
IZ0, IZ1 = PK_Z[0] + 0.7, PK_Z[1] - 1.0   # 12 Oct evening: still bowed front-to-back in the printed body, 0.6 mm shorter (was +0.3/-0.8)       # more room at the front: that face of the pocket is a long bridge and sagged on the first body (12 Oct: was -0.5)
IZ1_TAB = PK_Z[1] - 0.5                        # the tray-stop tab keeps its old front end, so the printed tray stop still fits
CHAMFER = 1.0                                  # 45 degree bevel on the four long corners: the printed body's pocket corners came out rounded (12 Oct)
HOLE_Z = (PL_ZC - INS["hole_d"] / 2, PL_ZC + INS["hole_d"] / 2)
HOLE_Y = (PL_YC - INS["hole_w"] / 2, PL_YC + INS["hole_w"] / 2)
ins = bx(IX0, IX1, IY0, IY1, IZ0, IZ1)
hole2d = rrect(PL_YC, PL_ZC, INS["hole_w"], INS["hole_d"], 4.0)            # (Y, Z)

# ---- drop beam: one sensor either side of the hole, in a pocket under the plate, looking across the hole.
# Each sensor slides in from the front, tip first, and its cable leaves forward into a groove in the plate.
BEAM_Z = PL_ZC
# After the first test fit (11 Oct): the pocket was a little tight on the sides and the bridged floor sagged, so more room both ways.
SP = dict(H=11.7, floor=1.6, corbel=9.0, wall=1.6, end=1.6, skin=1.0, gap=0.3, side=0.5, rail=1.4, seat=0.9, rib=0.55, pin_d=2.1, pin_h=3.4)
FACE = SEN["dome"] + SP["gap"] + SP["skin"]                 # block face to the edge of the hole
ZT = BEAM_Z - SEN["led_from_end"]                           # tip end of the sensor block
ZC = ZT + SEN["L"]                                          # cable end
PZ0, PZ1 = ZT - 0.2 - SP["end"], ZC + 6.3                   # pocket, closed end .. open end; the last 6 mm shield the cable where it climbs into its groove
PIN_Z = ZC + 0.25 + SP["pin_d"] / 2                         # a piece of 1.75 mm filament pushed through here keeps the sensor in
D_OUT = SEN["T"] + SP["side"]                               # inner face of the outer wall
BEAM_X = IX1 + SP["H"] - SEN["W"] / 2                       # the block rests on the pocket floor
YFACE = (HOLE_Y[0] - FACE, HOLE_Y[1] + FACE)                # block faces, left and right


def pocket(right):
    """d = distance from the block face, away from the hole; h = distance below the plate"""
    yf = YFACE[1] if right else YFACE[0]
    s = 1.0 if right else -1.0

    def b(d0, d1, h0, h1, z0, z1):
        ya, yb = yf + s * d0, yf + s * d1
        return bx(IX1 + h0, IX1 + h1, min(ya, yb), max(ya, yb), z0, z1)
    H, FL = SP["H"], SP["floor"]
    add = [b(-FACE, -FACE + SP["skin"], -0.5, H + FL, PZ0, PZ1),                          # skin on the hole side: shrouds the bulb, carries the floor
           b(-FACE, 0.0, -0.5, SP["rail"], PZ0, PZ1),                                     # rail the block face runs along, clear of the bulb
           b(-FACE, 0.0, -0.5, H + FL, PZ0, ZT - 0.2 + SP["seat"]),                       # seat for the tip of the block
           b(-FACE, D_OUT + SP["wall"], -0.5, H + FL, PZ0, PZ0 + SP["end"]),              # closed end
           b(D_OUT, D_OUT + SP["wall"], -0.5, H + FL, PZ0, PZ1),                          # outer wall
           b(-FACE, D_OUT + SP["wall"], H, H + FL, PZ0, PZ1)]                             # floor (a bridge when printed)
    def p2(pts, z0, z1):                                                               # prism from (d, h) points, along Z
        return prism(Polygon([(IX1 + h, yf + s * d) for d, h in pts]).buffer(0), z0, z1, "xy")

    def ramp(zz):                                                                      # crush rib with a lead-in on the entry side: 0.05 mm squeeze (the filament pin does the holding)
        pts = [(D_OUT + 0.3, zz), (D_OUT - SP["rib"], zz), (D_OUT - SP["rib"], zz + 0.8), (D_OUT + 0.3, zz + 2.9)]
        return prism(Polygon([(yf + s * d, z) for d, z in pts]).buffer(0), IX1, IX1 + H, "yz")
    add += [ramp(zz) for zz in (ZT + 6.0, ZC - 5.0)]
    di = -FACE + SP["skin"]
    add.append(p2([(di - 0.05, SP["corbel"] - 0.05), (di + H - SP["corbel"], H), (di + H - SP["corbel"], H + 0.1), (di - 0.05, H + 0.1)], PZ0, PZ1))   # 45 degree corbel under the floor: a shorter bridge to print; clears the bulb
    win = prism(rrect(IX1 + H - SEN["W"] / 2, BEAM_Z + 0.4, 7.0, 8.4, 2.0), min(yf, yf - s * (FACE + 1)), max(yf, yf - s * (FACE + 1)), "xz")
    ya, yb = yf - s * (FACE + 1), yf + s * (D_OUT + SP["wall"] + 1)
    pin = prism(Point(IX1 + SP["pin_h"], PIN_Z).buffer(SP["pin_d"] / 2, quad_segs=8), min(ya, yb), max(ya, yb), "xz")
    return add, [win, pin]


GRV_W = 3.6
GRV_YC = (YFACE[0] - SEN["T"] / 2, YFACE[1] + SEN["T"] / 2)                # where each cable leaves its sensor
GRV_Z0 = ZC
grooves = []
for gc in GRV_YC:
    g0, g1 = gc - GRV_W / 2, gc + GRV_W / 2
    grooves.append(D(bx(IX1 - 3.0, IX1 + 1, g0, g1, GRV_Z0, IZ1 + 1),
                     bx(IX1 - 3.1, IX1 + 2, g0 - 0.1, g0 + 1.4, PZ1 + 0.4, PZ1 + 2.0),         # two staggered nubs make the cable zigzag, which holds it in:
                     bx(IX1 - 3.1, IX1 + 2, g1 - 1.4, g1 + 0.1, PZ1 + 3.0, PZ1 + 4.6)))        # 2.2 mm clear past each, full depth
PEG_Y = (peg_slot[2] + 0.3, peg_slot[3] - 0.3)                 # the stock peg is 11.9 wide in a 12.5 slot
PEG_GAP, RAIL_W, RAIL_H = 0.2, 1.6, 4.0
# 13 Oct: the side rails alone let the peg sag between the back wall and the lid, so the rails now run the full height of the peg
# and close underneath it: a sheath.  Floor sits just under the lowest the peg can be (PEG_X[1]), so it holds the peg up.
SHEATH = dict(gap=0.25, floor=1.6, z0=IZ0 + 0.5, z1=IZ1 - 0.5, lead=1.2, lead_len=4.0)
SH_FLOOR = PEG_X[1] + SHEATH["gap"]                              # top face of the sheath floor
SH_BOT = SH_FLOOR + SHEATH["floor"]
SH_Y = (PEG_Y[0] - PEG_GAP - RAIL_W, PEG_Y[1] + PEG_GAP + RAIL_W)
PEG_RELIEF = bx(peg_slot[0], SH_FLOOR, PEG_Y[0] - PEG_GAP, PEG_Y[1] + PEG_GAP, IZ0 - 1, IZ1 + 1)   # the channel inside the sheath
PEG_RAILS = [bx(IX1 - 0.5, SH_BOT, SH_Y[0], PEG_Y[0] - PEG_GAP, SHEATH["z0"], SHEATH["z1"]),
             bx(IX1 - 0.5, SH_BOT, PEG_Y[1] + PEG_GAP, SH_Y[1], SHEATH["z0"], SHEATH["z1"]),
             bx(SH_FLOOR - 0.01, SH_BOT, SH_Y[0], SH_Y[1], SHEATH["z0"], SHEATH["z1"])]
_L, _LL = SHEATH["lead"], SHEATH["lead_len"]
SH_LEADS = [prism(Polygon([(SH_FLOOR - 0.01, ze), (SH_FLOOR + _L, ze), (SH_FLOOR - 0.01, ze + sg * _LL)]).buffer(0), PEG_Y[0] - PEG_GAP, PEG_Y[1] + PEG_GAP, "xz")
            for ze, sg in ((SHEATH["z0"] - 0.01, 1), (SHEATH["z1"] + 0.01, -1))]      # ramped floor at both ends: a sagging peg tip rides up into it
ins = D(U(ins, *PEG_RAILS), prism(hole2d, IX0 - 1, IX1 + 1, "yz"), *grooves, PEG_RELIEF, *SH_LEADS)
TAB_Z = (HOLE_Z[1] + 0.02, IZ1_TAB)
ins = U(ins,
        prism(Polygon([(IX1 - 0.5, TAB_Z[0]), (IX1 + 4.5, TAB_Z[0]), (IX1 + 4.5, TAB_Z[1] - 1.2), (IX1 + 2.0, TAB_Z[1]), (IX1 - 0.5, TAB_Z[1])]),
              71.0, 88.0, "xz"))                                                      # the tray stop rests against this: the top of the light tray cannot tip back
ins = U(ins, bx(IX1 - 0.5, LEDGE_X, IY0, Y_LO - 0.2, IZ0, IZ1), bx(IX1 - 0.5, LEDGE_X, Y_HI + 0.2, IY1, IZ0, IZ1))      # feet on the two ledges
ins = U(ins, *[bx(IX0_OLD + 0.5, IX1, yy0, yy1, zz, zz + 0.8) for zz in (IZ0 + 8.0, IZ1 - 9.0) for yy0, yy1 in ((IY0 - 0.45, IY0 + 0.1), (IY1 - 0.1, IY1 + 0.45))])   # crush ribs: a push fit in the pocket
for right in (False, True):
    a_, c_ = pocket(right)
    ins = D(U(ins, *a_), *c_)
_c = CHAMFER
_bev = [prism(Polygon([(y0, z0), (y0 + sy * _c, z0), (y0, z0 + sz * _c)]), IX0 - 1, LEDGE_X + 1, "yz")
        for (y0, sy) in ((IY0 - 0.01, 1), (IY1 + 0.01, -1)) for (z0, sz) in ((IZ0 - 0.01, 1), (IZ1 + 0.01, -1))]
ins = D(ins, *_bev)
# 13 Oct: one-piece top.  A funnel is cut into the (now flush) top face: 9 mm wide round the hole, 12 mm deep, about 53 degrees.
# Printed top face down, the funnel walls are a 37-degree-from-vertical overhang, so no supports.  A 1.5 mm skin on the funnel is yellow.
FUN = dict(band=9.0, drop=12.0, skin=1.5)
FY = (HOLE_Y[0] - FUN["band"], HOLE_Y[1] + FUN["band"])
def _fun_hull(dx):
    pts = [(xx + dx, yy, zz) for xx in (IX0 - 6.0, IX0) for yy in FY for zz in (IZ0 - 1.0, IZ1 + 1.0)]
    pts += [(IX0 + FUN["drop"] + dx, y, z) for y, z in np.asarray(hole2d.exterior.coords)]
    return from_tri(trimesh.convex.convex_hull(np.array(pts)))
_fh = _fun_hull(0.0)                                   # the funnel surface the duck sees
LIN = dict(t=2.67, gap=0.3)                            # liner: 2.67 mm measured vertically = 1.6 mm through the 53-degree wall
_seat = I(_fun_hull(LIN["t"] + LIN["gap"]), bx(IX0 - 7, IX0 + FUN["drop"] + LIN["gap"], IY0 - 1, IY1 + 1, IZ0 - 2, IZ1 + 2))
ins_liner = fix(I(D(_fun_hull(LIN["t"]), _fh, prism(hole2d, IX0 - 1, IX1 + 1, "yz")), bx(IX0, IX0 + FUN["drop"], IY0, IY1, IZ0, IZ1)))
ins = D(ins, _seat)                                    # black insert: same funnel, cut deeper by the liner, with a flat seat round the hole
solid("insert", ins)
solid("funnel_liner", ins_liner)
ins_black, ins_yellow = ins, ins_liner
META["insert_funnel"] = dict(Y=[round(v, 2) for v in FY], drop=FUN["drop"], slope_deg=round(float(np.degrees(np.arctan(FUN["drop"] / FUN["band"]))), 1),
                             lip_below=round(IX1 - IX0 - FUN["drop"] - LIN["gap"], 2), liner_cm3=round(abs(ins_liner.volume) / 1000, 2), liner_seat_ring=round(0.75 * LIN["t"], 2))
assert YFACE[0] - D_OUT - SP["wall"] > peg_slot[3] + 3.0, "sensor pocket too close to the locking peg"
META["insert"] = dict(X=[round(IX0, 2), round(IX1, 2)], peg_top_range=[round(peg_slot[0], 2), round(peg_slot[0] + 0.63, 2)], relief_to=14.5, feet_to=LEDGE_X,
                      side_clearance=0.3, rims=[round(HOLE_Z[0] - IZ0, 2), round(IZ1 - HOLE_Z[1], 2)],
                      pocket_hangs_below_plate=round(SP["H"] + SP["floor"], 2), pin_Z=round(PIN_Z, 2), pin_X=round(IX1 + SP["pin_h"], 2), floor_bridge_span=round(D_OUT - (-FACE + SP["skin"] + SP["H"] - SP["corbel"]), 2), pocket_Y_left=[round(YFACE[0] - D_OUT - SP["wall"], 2), round(HOLE_Y[0], 2)],
                      pocket_Z=[round(PZ0, 2), round(PZ1, 2)], groove_Y=[[round(g - GRV_W / 2, 2), round(g + GRV_W / 2, 2)] for g in GRV_YC])
META["sheath"] = dict(X=[round(IX1, 2), round(SH_BOT, 2)], floor_top=round(SH_FLOOR, 2), Y=[round(v, 2) for v in SH_Y], Z=[round(SHEATH["z0"], 2), round(SHEATH["z1"], 2)], inner_w=round(PEG_Y[1] - PEG_Y[0] + 2 * PEG_GAP, 2))
META["drop_beam"] = dict(X_from_top=round(BEAM_X, 2), Z=round(BEAM_Z, 2), across="Y", span_between_bulb_tips=round(HOLE_Y[1] - HOLE_Y[0] + 2 * (SP["gap"] + SP["skin"]), 2),
                         bulb_tip_behind_hole_edge=round(SP["gap"] + SP["skin"], 2), sensor_play_up_down=round(SP["H"] - SEN["W"], 2))

# ================================================================== TRAY STOP (pushed through a window in the light tray from behind)
# Keeps the top of the light tray from tipping back into the chamber: its shoulder rests against the tab on the insert.
TRAY_Z0, TRAY_T = 67.4, 0.8
TRAY_ZI = TRAY_Z0 + TRAY_T
RX_FACE = TAB_Z[1] + 0.3                                   # 0.3 in front of the insert's stop tab
WIN_CL = 0.25
STOPW = (23.5, 71.0, 30.0, 88.0)                           # window in the tray: X0, Y0, X1, Y1
stop = U(bx(STOPW[0] + WIN_CL, STOPW[2] - WIN_CL, STOPW[1] + WIN_CL, STOPW[3] - WIN_CL, TRAY_Z0 - 0.5, FRONT),          # the part that passes through the window
         bx(16.6, 31.2, STOPW[1] - 1.2, STOPW[3] + 1.2, RX_FACE, TRAY_Z0 - 0.1))                                       # shoulder that stays behind the tray
SR = 0.40                                                  # crush ribs so the stop stays in the window on its own: 0.15 mm squeeze
for yw, sg in ((STOPW[1] + WIN_CL, -1.0), (STOPW[3] - WIN_CL, 1.0)):
    for xr in (STOPW[0] + 1.4, STOPW[2] - 2.2):
        pts = [(yw - sg * 0.3, TRAY_Z0 - 0.3), (yw + sg * SR, TRAY_Z0 - 0.3), (yw + sg * SR, TRAY_ZI + 1.6), (yw - sg * 0.3, TRAY_ZI + 4.6)]
        stop = U(stop, prism(Polygon(pts).buffer(0), xr, xr + 0.8, "yz"))
stop = D(stop, prism(Polygon([(16.5, RX_FACE - 0.1), (16.5, RX_FACE + 1.3), (17.9, RX_FACE - 0.1)]), STOPW[1] - 2, STOPW[3] + 2, "xz"))      # lead-in for the insert's tab
solid("tray_stop", stop)
META["tray_stop"] = dict(face_Z=round(RX_FACE, 2), window=STOPW)

# ================================================================== LIGHT TRAY (white): light box, LED block, wire duct, top arm
DECK_T = 1.6
BAY_H = 28.2                         # deck + walls = 29.8 mm = a whole number of 0.2 mm layers
DECK_X1 = DIV_TOP - BAY_H            # underside of the deck
DECK_X0 = DECK_X1 - DECK_T           # top of the deck
Cxy = uv2xy(C)
DUCT_Y = (35.8, 47.2)                # 9 mm wide inside
ARM_X = (14.5, 31.7)
ARM_Y1 = 133.0                       # the arm reaches past the right-hand sensor cable
ARM_ROOF_Y1 = 121.0                  # no upper wall beyond this: the right-hand cable drops in here
TONGUE_X1 = 134.3                    # short, so a USB plug can reach the board past it
LED_U = [64.0, 73.0, 82.0, 91.0]
led_tip_X = []
for u in LED_U:
    seg = LineString([(u, 0), (u, 200)]).intersection(C)
    v_edge = seg.bounds[3]
    led_tip_X.append(v_edge - 1.2 + LID_X0)
TROUGH_X0 = max(led_tip_X) + LED["L"] + 0.3
assert DECK_X0 - 0.8 - TROUGH_X0 >= 3.0, ("no room for the LED legs", DECK_X0 - 0.8 - TROUGH_X0)
LEDBLK = sbox(min(led_tip_X) - 3.0, LED_U[0] + LID_Y0 - 5.0, TROUGH_X0, LED_U[-1] + LID_Y0 + 5.0)
TROUGH_Y1 = LED_U[-1] + LID_Y0 + 5.0

plate2d = unary_union([Cxy.buffer(0.6), sbox(ARM_X[0], DUCT_Y[0], ARM_X[1], ARM_Y1), sbox(ARM_X[0], DUCT_Y[0], DECK_X0, DUCT_Y[1]), sbox(ARM_X[1] - 1, DUCT_Y[1] - 1, DECK_X0, 70.0),
                       sbox(min(led_tip_X) - 3.0, DUCT_Y[1], DECK_X0, TROUGH_Y1 + 1.2)])
plate2d = plate2d.buffer(7.0).buffer(-7.0).intersection(sbox(ARM_X[0], DUCT_Y[0], DECK_X0, 200))
plate2d = unary_union([plate2d, sbox(DECK_X0 - 0.1, DUCT_Y[0], TONGUE_X1, DUCT_Y[1])])
NOTCH = sbox(*STOPW)                                                      # closed window for the tray stop
HOLDER_FP = NOTCH.buffer(0.3, join_style=2)
plate2d = plate2d.difference(NOTCH)
assert plate2d.geom_type == "Polygon", plate2d.geom_type

rim = Cxy.buffer(0.6).difference(Cxy.buffer(-1.0))
walls2d = [rim,
           sbox(ARM_X[0], DUCT_Y[0], TONGUE_X1, DUCT_Y[0] + 1.2),                                  # duct, left wall
           sbox(ARM_X[1] - 1.2, DUCT_Y[1] - 1.2, TROUGH_X0, DUCT_Y[1]),                            # duct, right wall (open to the arm above and the trough below)
           sbox(DECK_X0 - 0.8, DUCT_Y[1] - 1.2, TONGUE_X1, DUCT_Y[1]),
           sbox(ARM_X[1] - 1.2, DUCT_Y[1] - 1.2, ARM_X[1], ARM_Y1),                                # arm, lower wall
           sbox(ARM_X[0], DUCT_Y[1], ARM_X[0] + 1.2, ARM_ROOF_Y1),                                 # arm, upper wall
           sbox(ARM_X[0], ARM_Y1 - 1.2, ARM_X[1], ARM_Y1),
           LEDBLK.difference(Cxy.buffer(0.6)),
           sbox(DECK_X0 - 0.8, DUCT_Y[1], DECK_X0, TROUGH_Y1 + 1.2),                               # trough floor
           sbox(TROUGH_X0 - 0.1, TROUGH_Y1, DECK_X0, TROUGH_Y1 + 1.2)]
walls2d = unary_union(walls2d).difference(HOLDER_FP)
led_cuts2d, flange2d = [], []
for u, tx in zip(LED_U, led_tip_X):
    y = u + LID_Y0
    led_cuts2d += [sbox(tx - 9.0, y - 2.6, tx + LED["L"] - LED["flange_t"] + 0.1, y + 2.6),          # body, and the light path above it
                   sbox(tx + LED["L"] - LED["flange_t"], y - 3.1, tx + LED["L"] + 0.3, y + 3.1),     # flange
                   sbox(tx + LED["L"], y - 2.2, TROUGH_X0 + 0.2, y + 2.2)]                           # legs
    flange2d.append(sbox(tx + LED["L"] - LED["flange_t"], y - 3.1, tx + LED["L"] + 0.3, y + 3.1))
walls2d = walls2d.difference(unary_union(led_cuts2d))
# two pips per LED slot so the LED is a light push fit
pips = []
for u, tx in zip(LED_U, led_tip_X):
    y = u + LID_Y0
    for s in (-1, 1):
        pips.append(sbox(tx + 3.0, y + s * 2.6 - (0.12 if s > 0 else 0.3), tx + 4.0, y + s * 2.6 + (0.12 if s < 0 else 0.3)))
walls2d = unary_union([walls2d, unary_union(pips)]).buffer(0)
walls2d = unary_union([g for g in (walls2d.geoms if hasattr(walls2d, "geoms") else [walls2d]) if g.area > 0.2])
import pickle; pickle.dump(dict(plate=plate2d, walls=walls2d, C=Cxy, notch=NOTCH), open(os.path.join(outdir, "tray2d.pkl"), "wb"))
_off = walls2d.difference(plate2d.buffer(0.01))
assert _off.area < 0.01, ("a wall hangs off the tray plate", _off.bounds, _off.area)
tray = U(prism(plate2d, TRAY_Z0, TRAY_ZI, "xy"), prism(walls2d, TRAY_ZI - 0.01, FRONT, "xy"))
tray = D(tray, prism(unary_union(flange2d), TRAY_ZI - 0.3, TRAY_ZI + 0.1, "xy"),
         bx(DECK_X1 - 0.3, TONGUE_X1 + 1, DUCT_Y[0] - 1, DUCT_Y[1] + 1, FRONT - 1.3, FRONT + 1))        # the tongue ducks behind a bar on the electronics tray
# 13 Oct (v19): the trough alone is too tight for legs + resistors.  The tray's back plate is now open behind the trough, so the
# legs bend back into a separate LED HOOD that sits on the deck behind the tray (glued to the tray's back), and the wires slide
# down its sloped roof through a long slot in the deck into the electronics bay: behind the board, and over the battery.
WINDOW_Y = (DUCT_Y[1] + 1.2, TROUGH_Y1)
tray = D(tray, bx(TROUGH_X0, DECK_X0 - 0.8, WINDOW_Y[0], WINDOW_Y[1], TRAY_Z0 - 1.0, TRAY_ZI + 0.02))
META["led_trough"] = dict(height=round(DECK_X0 - 0.8 - TROUGH_X0, 2), window_X=[round(TROUGH_X0, 2), round(DECK_X0 - 0.8, 2)], window_Y=[round(v, 2) for v in WINDOW_Y])
TRAY_LIFT = TONGUE_X1 - DECK_X1 + 0.3                                                                # the tray goes in this much high, then drops
solid("light_tray", tray)
LED_Z = TRAY_ZI + 0.15 + LED["d"] / 2
META["tray_lift"] = round(TRAY_LIFT, 2)
META["leds"] = dict(tip_X=[round(x, 2) for x in led_tip_X], Y=[round(u + LID_Y0, 2) for u in LED_U], leg_room=round(DECK_X0 - 0.8 - TROUGH_X0, 2), axis_Z=round(LED_Z, 2))

# ================================================================== LED HOOD (white): sits on the deck behind the light tray
HOOD = dict(front=TRAY_Z0 + 0.01, roof_front_X=115.5, roof_back_Z=52.2, t=1.2, y0=DUCT_Y[0], y1=116.2, cap=2.0, slot_front=60.6)
_A = np.array([HOOD["roof_front_X"], HOOD["front"]]); _B = np.array([DECK_X0, HOOD["roof_back_Z"]])
_d = (_B - _A) / np.linalg.norm(_B - _A); _n = np.array([-_d[1], _d[0]])
if _n[0] > 0: _n = -_n                                                      # normal pointing up (smaller X)
_Ao = _A + HOOD["t"] * _n; _Bo = _B + HOOD["t"] * _n
_Ao = _Ao + (_A[1] - _Ao[1]) / (-_d[1]) * (-_d)                             # roof outer line, cut at the hood front
_Bo = _Bo + (DECK_X0 - _Bo[0]) / _d[0] * _d                                 # and at the deck
roof2d = Polygon([tuple(_A), tuple(_Ao), tuple(_Bo), tuple(_B)])           # (X, Z)
side2d = Polygon([tuple(_Ao), (DECK_X0, _A[1]), tuple(_Bo)])
flange2d = Polygon([(_Ao[0], _A[1]), (TROUGH_X0 - 0.6, _A[1]), (TROUGH_X0 - 0.6, _A[1] - 1.2), (_Ao[0] + 1.2 * abs(_d[0] / _d[1]), _A[1] - 1.2)])
fw2d = Polygon([(_Ao[0], _A[1]), (DECK_X0, _A[1]), (DECK_X0, _A[1] - 1.2), (_Ao[0] + 1.2 * abs(_d[0] / _d[1]), _A[1] - 1.2)])
PLATE_Y1 = TROUGH_Y1 + 1.2                                                  # the tray plate stops here; the hood closes its own front beyond
PEG = dict(w=1.6, len=4.0, depth=1.2, z=62.6)
hood_parts = [prism(roof2d, HOOD["y0"], HOOD["y1"], "xz"),
              prism(side2d, HOOD["y0"], HOOD["y0"] + HOOD["cap"], "xz"), prism(side2d, HOOD["y1"] - HOOD["cap"], HOOD["y1"], "xz"),
              prism(flange2d, HOOD["y0"], PLATE_Y1, "xz"),
              prism(fw2d, PLATE_Y1 - 0.01, HOOD["y1"], "xz")]
PEG_YC = (HOOD["y0"] + HOOD["cap"] / 2, HOOD["y1"] - HOOD["cap"] / 2)

led_hood = U(*hood_parts)
solid("led_hood", led_hood)
# v20: the hood is part of the light tray (one print, standing on the duct end)
tray = U(tray, led_hood)
solid("light_tray", tray)
_tan = abs(_d[0] / _d[1])                                                  # X per Z along the roof
SLOT = dict(y0=52.0, y1=114.0)
SLOT["back_bottom_Z"] = HOOD["roof_back_Z"] - DECK_T / _tan
slot2d = Polygon([(DECK_X0 - 0.5, HOOD["roof_back_Z"] + 0.5 / _tan), (DECK_X1 + 0.5, SLOT["back_bottom_Z"] - 0.5 / _tan), (DECK_X1 + 0.5, HOOD["slot_front"]), (DECK_X0 - 0.5, HOOD["slot_front"])])
throat = float(Point(DECK_X0, HOOD["slot_front"]).distance(LineString([tuple(_A), tuple(_B)])))
META["led_hood"] = dict(Y=[round(HOOD["y0"], 2), round(HOOD["y1"], 2)], Z=[round(HOOD["roof_back_Z"], 2), round(HOOD["front"], 2)], roof_slope_deg=round(float(np.degrees(np.arctan(_tan))), 1),
                        inside_height_at_front=round(DECK_X0 - HOOD["roof_front_X"], 2), slot_Y=[SLOT["y0"], SLOT["y1"]], slot_Z_top=[round(HOOD["roof_back_Z"], 2), HOOD["slot_front"]],
                        slot_Z_bottom=[round(SLOT["back_bottom_Z"], 2), HOOD["slot_front"]], throat_across=round(throat, 2), chamber_lost_at_deck=round(HOOD["front"] - HOOD["roof_back_Z"], 1))

# ================================================================== ELECTRONICS TRAY (deck + walls, stands on the divider)
dy0, dy1 = Y_LO + 0.5, Y_HI - 0.5
dz0 = Z_BACK + 0.55
W_BOT = DIV_TOP
SOCK = (DUCT_Y[0] - 0.3, DUCT_Y[1] + 0.3)
GUIDE_R = (106.5, 107.7)
GUIDE_L = (50.0, 51.2)
OUT_R = (BAT_Y[1], BAT_Y[1] + 1.2)
CART_STOP_Z = FRONT - 0.9
carrier = U(
    bx(DECK_X0, DECK_X1, dy0, dy1, dz0, FRONT),
    bx(DECK_X1 - 0.01, W_BOT, 20.0, 21.2, dz0, 45.0),                                       # left leg, set in from the wall so it cannot trap the shelf-sensor wires
    bx(DECK_X1 - 0.01, W_BOT, dy0, GUIDE_R[1], dz0, dz0 + 1.2),                             # rear wall behind the board and wiring bays
    bx(DECK_X1 - 0.01, W_BOT, GUIDE_R[0], GUIDE_R[1], dz0, CART_STOP_Z),                    # wall between board bay and battery slot
    bx(DECK_X1 - 0.01, W_BOT, OUT_R[0], OUT_R[1], dz0, FRONT),                              # outer wall of the battery slot
    bx(DECK_X1 - 0.01, TONGUE_X1 + 0.5, SOCK[0] - 1.2, SOCK[0], TRAY_Z0 - 1.5, FRONT),      # socket for the light tray's tongue
    bx(DECK_X1 - 0.01, TONGUE_X1 + 0.5, SOCK[1], SOCK[1] + 1.2, TRAY_Z0 - 1.5, FRONT),
    bx(DECK_X1 - 0.01, TONGUE_X1 + 0.5, SOCK[0] - 1.2, SOCK[1] + 1.2, TRAY_Z0 - 1.5, TRAY_Z0 - 0.3),
    bx(DECK_X1 - 0.01, TONGUE_X1 + 0.5, SOCK[0] - 1.2, SOCK[1] + 1.2, FRONT - 1.0, FRONT),  # bar across the front of the socket: the tray cannot fall forward
    bx(DECK_X1 - 0.01, DECK_X1 + 5.0, GUIDE_L[0], GUIDE_L[1], 60.0, CART_STOP_Z))           # left stop for the screen cartridge
carrier = D(carrier,
            bx(DECK_X0 - 1, DECK_X1 + 0.02, SOCK[0], SOCK[1], TRAY_Z0 - 0.3, FRONT + 1),    # notch in the deck for the tongue
            bx(DECK_X1, DECK_X1 + 7.0, GUIDE_R[0] - 1, GUIDE_R[1] + 1, 48.0, 66.0),         # wire pass from the battery slot
            bx(DECK_X1, DECK_X1 + 10.0, OUT_R[0] - 1, OUT_R[1] + 1, 52.0, FRONT - 1.5),     # window where the right-hand shelf-sensor wire comes out of its wall groove
            prism(slot2d, SLOT["y0"], SLOT["y1"], "xz"))                                   # sloped wire slot under the LED hood
# v21: light tray (with its LED hood) and electronics tray are one part; it slides in from the front in one go
carrier = D(carrier, prism(slot2d, SLOT["y0"], SLOT["y1"], "xz"))
carrier = U(carrier, P["light_tray"])
carrier = D(carrier, prism(slot2d, SLOT["y0"], SLOT["y1"], "xz"))          # keep the wire slot clear of the hood edge
solid("light_tray", carrier)
del P["led_hood"]
META["deck_top_X"] = round(DECK_X0, 2)
META["pile_room_under_beam"] = round(DECK_X0 - BEAM_X - 5, 1)
META["battery_slot"] = dict(width=round(BAT_Y[1] - BAT_Y[0], 2), height=round(BAY_H, 2), length=round(FRONT - BAT_POCKET_Z, 2))

# ================================================================== SCREEN CARTRIDGE (holds the Heltec; slides in from the front)
MASK_T = 0.8
MASK_Z0 = FRONT - MASK_T                          # the glass rests against this face
CART_X0 = DECK_X1 + 0.4
CART_Z0 = 60.8
RAIL_T = 0.8
cl = 0.3
yL0, yL1 = BRD_Y0 - cl - 1.2, BRD_Y0 - cl          # USB-end wall
yR0, yR1 = BRD_Y1 + cl, BRD_Y1 + cl + 1.2          # antenna-end wall
assert yL0 > GUIDE_L[1] + 0.1 and yR1 < GUIDE_R[0] - 0.1, (yL0, yR1)
MASK_Y0 = YC - BRD["oled"][0] / 2 - 2.0
USB_X = (0.5 * (BRD_X0 + BRD_X1) - 6.75, 0.5 * (BRD_X0 + BRD_X1) + 6.75)
RAIL_IN = CART_X0 + RAIL_T                         # inner face of the top rail
cart = U(
    bx(BRD_X1, W_BOT, yL0, yR1, CART_Z0, FRONT),                                             # rail the board stands on
    bx(CART_X0, W_BOT, yR0, yR1, CART_Z0, FRONT),                                            # antenna-end wall
    bx(CART_X0, W_BOT, yL0, yL1, CART_Z0, FRONT),                                            # USB-end wall (cut for the plug below)
    bx(CART_X0, W_BOT, MASK_Y0, GUIDE_R[1] + 0.2, MASK_Z0, FRONT),                           # mask in front of the glass, with a stop ear on the right
    bx(CART_X0, CART_X0 + 5.0, GUIDE_L[0] - 0.2, yL1, MASK_Z0, FRONT),                       # stop ear on the left
    bx(W_BOT - 2.0, W_BOT, yL0, MASK_Y0 + 0.1, MASK_Z0, FRONT),                              # low lip under the buttons, ties the front together
    bx(CART_X0, RAIL_IN, yL0, yR1, CART_Z0, FRONT))                                          # top rail: makes the cartridge a closed frame
cart = D(cart,
         prism(rrect(GLASS_X, YC, WIN_MASK[1], WIN_MASK[0], 1.0), MASK_Z0 - 1, FRONT + 1, "xy"),
         bx(USB_X[0], USB_X[1], yL0 - 3, yL1 + 0.01, CART_Z0 - 1, FRONT + 1))
ribs = []
for xx in (CART_X0 + 2.0, W_BOT - 4.5):
    ribs += [bx(xx, xx + 0.8, yL1 - 0.01, yL1 + 0.4, CART_Z0, MASK_Z0), bx(xx, xx + 0.8, yR0 - 0.4, yR0 + 0.01, CART_Z0, MASK_Z0)]
# Snap ledges behind the pin-header plastic so the board cannot work its way backward.  They sit only where no jumper plug goes
# (header positions 5-8 and 12-15 on both rows), and leave room for the thickest board in the estimate range.
LEDGE = dict(reach=0.8, segs=((14.0, 19.5), (32.0, 38.0)))
LEDGE_STOP_Z = MASK_Z0 - (BRD["face_to_back"] + 0.75) - 2.54 - 0.2
za = LEDGE_STOP_Z - 0.5                                                                      # apex of the triangular ledge
for y0, y1 in LEDGE["segs"]:
    ribs.append(prism(Polygon([(BRD_X1 + 0.01, za + 0.81), (BRD_X1 - LEDGE["reach"], za), (BRD_X1 + 0.01, za - 0.81)]), BRD_Y0 + y0, BRD_Y0 + y1, "xz"))
    ribs.append(prism(Polygon([(RAIL_IN - 0.01, za + 0.81), (RAIL_IN + LEDGE["reach"], za), (RAIL_IN - 0.01, za - 0.81)]), BRD_Y0 + y0, BRD_Y0 + y1, "xz"))
cart = U(cart, *ribs)
solid("screen_cartridge", cart)
assert 0.2 <= BRD_X0 - RAIL_IN <= 0.4, ("board clearance under the top rail", BRD_X0 - RAIL_IN)
META["cartridge"] = dict(window=WIN_MASK, mask_back_Z=round(MASK_Z0, 2), pocket_len=round(yR0 - yL1, 2), pocket_h=round(BRD_X1 - RAIL_IN, 2), height=round(W_BOT - CART_X0, 2),
                         under_deck=round(CART_X0 - DECK_X1, 2), ledge_gap=round(BRD_X1 - RAIL_IN - 2 * LEDGE["reach"], 2),
                         ledge_slack_for_estimate=round(MASK_Z0 - BRD["face_to_back"] - 2.54 - (za + 0.5), 2))

# ================================================================== SHELF SENSOR BLOCKS (two the same; slide into the side-wall channels)
ax0, ax1 = SH["X"] - SH["mouth"] / 2 + 0.15, SH["X"] + SH["mouth"] / 2 - 0.15
face = Y_LO
ybot = face - SH["depth"] + 0.15
prot = 1.2 + SEN["T"] + 0.7 + SEN["dome"] + 0.3 - (face - ybot)        # how far it stands into the shelf: the bulb tip ends 0.3 inside the block
prof = Polygon([(ax0, face), (ax0 - SH["flare"] * (face - ybot) / SH["depth"], ybot), (ax1 + SH["flare"] * (face - ybot) / SH["depth"], ybot), (ax1, face),
                (ax1, face + prot), (ax0, face + prot)])
scx = (SH["X"] - SEN["W"] / 2 - 0.3, SH["X"] + SEN["W"] / 2 + 0.3)
scy = (ybot + 1.2, ybot + 1.2 + SEN["T"] + 0.7)
LENS = (9.0, 9.5)                                            # slot the bulb and its ring ride in; the collar rubbed in the first 6.8 mm slot
RIB = 0.40                                                   # sensor crush ribs: 0.1 mm squeeze
SH_FRONT = 87.5
blk_front = SH_FRONT - 1.5
led_z = blk_front - SEN["led_from_end"]
shb = prism(prof, SH["z0"] + 0.2, SH_FRONT, "xy")
shb = D(shb,
        bx(scx[0], scx[1], scy[0], scy[1], SH["z0"] - 1, blk_front),
        bx(SH["X"] - LENS[0] / 2, SH["X"] + LENS[0] / 2, scy[1] - 0.1, face + prot + 1, SH["z0"] - 1, led_z + LENS[1] / 2),   # slot the lens rides in
        bx(ax0 - 3, ax1 + 3, ybot + 1.2, face + 0.3, SH["z0"] - 1, SH["z0"] + 6.0))                           # wires out to the tunnel, either side
fl = SH["flare"] * (face - ybot) / SH["depth"]
ymid = 0.5 * (face + ybot)
flank_ribs = unary_union([Point(ax0 - fl / 2 - 0.02, ymid).buffer(0.4, quad_segs=6), Point(ax1 + fl / 2 + 0.02, ymid).buffer(0.4, quad_segs=6)])   # stand 0.25 mm into the dovetail flanks
shb = U(shb, *[bx(xx0, xx1, scy[0], scy[1], zz, zz + 0.8) for zz in (blk_front - 17.0, blk_front - 11.0) for xx0, xx1 in ((scx[0] - 0.01, scx[0] + RIB), (scx[1] - RIB, scx[1] + 0.01))],
        *[bx(xx, xx + 0.8, ybot - 0.25, ybot + 0.01, SH["z0"] + 6.2, SH_FRONT) for xx in (SH["X"] - 5.0, SH["X"] + 4.2)],
        prism(flank_ribs, SH["z0"] + 6.2, SH_FRONT - 2.0, "xy"))
shb = D(shb, prism(Polygon([(face - 0.01, SH["z0"] + 0.1), (face + prot + 0.2, SH["z0"] + 0.1), (face + prot + 0.2, SH["z0"] + 0.2 + prot + 0.21)]),
                   ax0 - 1, ax1 + 1, "yz"))                                                  # 45 degree ramp on the back end, so a duck pulled forward rides over it
solid("shelf_block_L", shb)
R180 = np.array([[-1, 0, 0, 2 * SH["X"]], [0, -1, 0, 168.75], [0, 0, 1, 0], [0, 0, 0, 1.0]])
shr = shb.copy(); shr.apply_transform(R180)
solid("shelf_block_R", shr)
META["shelf_beam"] = dict(X=SH["X"], Z=round(led_z, 2), block_stands_into_shelf=round(prot, 2), opening_between_blocks=round(Y_HI - Y_LO - 2 * prot, 1))

# ================================================================== small extras
def ring(od, idia, t):
    return D(cyl_z(od, 0, t, 0, 0, 64), cyl_z(idia, -1, t + 1, 0, 0, 48))


EXTRA = dict(spacer_1mm=ring(10.0, 4.4, 1.0), spacer_2mm=ring(10.0, 4.4, 2.0))
coupon = D(bx(0, 52, 0, 52, 0, Z_BACK), cyl_z(MAG["recess_d"], -1, MAG["recess_depth"], 26, 26), cyl_z(MAG["screw_clear"], -1, 7, 26, 26, 48))
EXTRA["magnet_test_coupon"] = coupon

# ================================================================== stand-ins for bought parts (for checks and pictures only)
Q = {}
Q["pcb"] = bx(BRD_X0, BRD_X1, BRD_Y0, BRD_Y1, MASK_Z0 - BRD["face_to_back"], MASK_Z0 - BRD["face_to_back"] + BRD["pcb"])
Q["oled"] = bx(GLASS_X - BRD["oled"][1] / 2, GLASS_X + BRD["oled"][1] / 2, YC - BRD["oled"][0] / 2, YC + BRD["oled"][0] / 2, MASK_Z0 - BRD["face_to_back"] + BRD["pcb"], MASK_Z0)
pz0 = MASK_Z0 - BRD["face_to_tips"]
pcb_back = MASK_Z0 - BRD["face_to_back"]
P1 = 2.5                                             # first header pin, distance from the USB end (estimate from Heltec's pinout photo)
hy0, hy1 = BRD_Y0 + P1 - 1.27, BRD_Y0 + P1 - 1.27 + 18 * 2.54
Q["pins"] = U(bx(BRD_X0, BRD_X0 + 2.54, hy0, hy1, pcb_back - 2.54, pcb_back), bx(BRD_X1 - 2.54, BRD_X1, hy0, hy1, pcb_back - 2.54, pcb_back),      # header plastic
              bx(BRD_X0 + 0.95, BRD_X0 + 1.59, hy0, hy1, pz0, pcb_back - 2.5), bx(BRD_X1 - 1.59, BRD_X1 - 0.95, hy0, hy1, pz0, pcb_back - 2.5))      # the pins themselves
USED = dict(bottom=(1, 2, 3, 4, 9, 11, 17), top=(1, 10, 11, 17))     # header positions that get a jumper plug (bottom row = the 5V side)
plugs = []
for row, x0 in (("top", BRD_X0), ("bottom", BRD_X1 - 2.54)):
    for n in USED[row]:
        yc = BRD_Y0 + P1 + (n - 1) * 2.54
        plugs.append(bx(x0, x0 + 2.54, yc - 1.27, yc + 1.27, pcb_back - 2.54 - 14.0, pcb_back - 2.54))
Q["plugs"] = U(*plugs)
bz0 = BAT_POCKET_Z + 0.2
bx0 = DIV_TOP - HOLDER["T"]
Q["battery"] = U(bx(bx0, DIV_TOP, BAT_YC - HOLDER["W"] / 2, BAT_YC + HOLDER["W"] / 2, bz0, bz0 + HOLDER["L"]),
                 bx(bx0 + HOLDER["hole_from_face"] - 2.0, bx0 + HOLDER["hole_from_face"] + 2.0, WIRE_YC[0] - 2.0, WIRE_YC[0] + 2.0, bz0 - HOLDER["wire"], bz0 + 0.1))   # the wires, where they leave


def drop_sensor(right):
    yf = YFACE[1] if right else YFACE[0]
    s = 1.0 if right else -1.0
    x0 = IX1 + SP["H"] - SEN["W"]

    def yy(d0, d1):
        a, c = yf + s * d0, yf + s * d1
        return min(a, c), max(a, c)
    zb = ZC - SEN["boss_from_cable_end"]
    return U(bx(x0, x0 + SEN["W"], *yy(0.0, SEN["T"]), ZT, ZC),
             prism(Point(BEAM_X, BEAM_Z).buffer(SEN["bulb_d"] / 2, quad_segs=8), *yy(-SEN["dome"], 0.1), "xz"),           # drawn as a plain cylinder: the worst case
             prism(Point(BEAM_X, BEAM_Z).buffer(SEN["collar_d"] / 2, quad_segs=8), *yy(-SEN["collar_h"], 0.1), "xz"),
             prism(Point(BEAM_X, zb).buffer(SEN["boss_d"] / 2, quad_segs=8), *yy(-SEN["boss"], 0.1), "xz"))


Q["keeper"] = U(*[prism(Point(IX1 + SP["pin_h"], PIN_Z).buffer(0.875, quad_segs=8), *sorted((yf - s_ * (FACE - 0.1), yf + s_ * (D_OUT + SP["wall"] - 0.1))), "xz")
                  for yf, s_ in ((YFACE[0], -1.0), (YFACE[1], 1.0))])                                       # two bits of 1.75 mm filament
Q["sensor_drop_L"] = drop_sensor(False)
Q["sensor_drop_R"] = drop_sensor(True)
ssl = U(bx(SH["X"] - SEN["W"] / 2, SH["X"] + SEN["W"] / 2, scy[0], scy[0] + SEN["T"], blk_front - SEN["L"], blk_front),
        prism(Point(SH["X"], led_z).buffer(SEN["bulb_d"] / 2, quad_segs=8), scy[0] + SEN["T"] - 0.1, scy[0] + SEN["T"] + SEN["dome"], "xz"),
        prism(Point(SH["X"], led_z).buffer(SEN["collar_d"] / 2, quad_segs=8), scy[0] + SEN["T"] - 0.1, scy[0] + SEN["T"] + SEN["collar_h"], "xz"),
        prism(Point(SH["X"], blk_front - SEN["L"] + SEN["boss_from_cable_end"]).buffer(SEN["boss_d"] / 2, quad_segs=8), scy[0] + SEN["T"] - 0.1, scy[0] + SEN["T"] + SEN["boss"], "xz"))
ssr = ssl.copy(); ssr.apply_transform(R180)
Q["sensor_shelf"] = CAT(*[ssl, ssr])
leds = []
for u, tx in zip(LED_U, led_tip_X):
    c = prism(Point(u + LID_Y0, LED_Z).buffer(LED["d"] / 2, quad_segs=8), tx, tx + LED["L"], "yz"); leds.append(c)
Q["leds"] = CAT(*leds)
ps = peg_slot
Q["peg"] = bx(PEG_X[0], PEG_X[1], ps[2] + 0.3, ps[3] - 0.3, -1.0, 81.5)          # every place the peg can be, top at its highest
mags = []
for mx in MAG_ROWS_X:
    for my in MAG_COLS_Y:
        mags.append(cyl_z(MAG["d"], MAG["recess_depth"] - MAG["t"], MAG["recess_depth"] - 0.02, mx, my, 64))
        mags.append(cyl_z(8.0, Z_BACK, Z_BACK + 4.6, mx, my, 6))
Q["magnets"] = CAT(*mags)


def lid_at(zback):
    T = np.array([[0, 1, 0, LID_X0], [1, 0, 0, LID_Y0], [0, 0, 1, zback], [0, 0, 0, 1.0]])
    out = {}
    for k, m in list(LIDP.items()) + [("cap_yellow", cap10_y), ("cap_black", cap10_k)]:
        mm = m.copy(); mm.apply_transform(T); out[k] = mm
    return out


# ================================================================== checks
print("---- checks")
ok = True


def vol_overlap(a, b):
    try:
        r = I(a, b)
        return 0.0 if r.is_empty else abs(r.volume)
    except Exception as e:
        return float("nan")


names = list(P)
bad = []
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        a, b = P[names[i]], P[names[j]]
        if (a.bounds[0] > b.bounds[1]).any() or (b.bounds[0] > a.bounds[1]).any():
            continue
        v = vol_overlap(a, b)
        if not v < 0.5:
            bad.append((names[i], names[j], round(v, 2)))
print("printed part vs printed part overlaps (mm3 > 0.5):", bad)
CRUSH_OK = {("light_tray", "tray_stop"): 2.0, ("tray_stop", "light_tray"): 2.0, ("body", "shelf_block_L"): 12.0, ("body", "shelf_block_R"): 12.0, ("body", "insert"): 4.5, ("body", "funnel_liner"): 4.5}          # crush ribs: deliberate squeeze
bad = [b for b in bad if not ((b[0], b[1]) in CRUSH_OK and b[2] < CRUSH_OK[(b[0], b[1])])]
ok &= not bad

for zb, tag in ((Z_GRV0 + 0.002, "lid pushed fully back"), (LID_Z_NOM, "lid nominal")):
    L = lid_at(zb)
    pegv = vol_overlap(Q["peg"], bx(PEG_POCKET[1] + LID_X0, PEG_POCKET[3] + LID_X0, PEG_POCKET[0] + LID_Y0, PEG_POCKET[2] + LID_Y0, zb, zb + PEG_POCKET[4]))
    lidsolid = U(L["lid_black"], cap10.copy().apply_transform(np.array([[0, 1, 0, LID_X0], [1, 0, 0, LID_Y0], [0, 0, 1, zb], [0, 0, 0, 1.0]])))
    b2 = [(n, round(vol_overlap(P[n], lidsolid), 2)) for n in names if n != "body" and vol_overlap(P[n], lidsolid) > 0.3]
    print(f"{tag}: parts touching the lid:", b2)
    ok &= not b2
    vb = vol_overlap(P["body"], L["lid_black"])
    print(f"{tag}: lid vs body overlap {vb:.2f} mm3")

bq = []
for qn, qm in Q.items():
    for n in names:
        a = P[n]
        if (a.bounds[0] > qm.bounds[1]).any() or (qm.bounds[0] > a.bounds[1]).any():
            continue
        v = vol_overlap(a, qm)
        if v > 0.5:
            bq.append((qn, n, round(v, 1)))
RIB_OK = {("pcb", "screen_cartridge"): 1.0, ("battery", "body"): 1.5, ("sensor_drop_L", "insert"): 4.5, ("sensor_drop_R", "insert"): 4.5, ("light_tray", "tray_stop"): 2.0,
          ("sensor_shelf", "shelf_block_L"): 4.5, ("sensor_shelf", "shelf_block_R"): 4.5}
bq_bad = [b for b in bq if not ((b[0], b[1]) in RIB_OK and b[2] < RIB_OK[(b[0], b[1])])]
print("bought-part stand-ins cutting into printed parts:", bq, "| unexpected:", bq_bad)
ok &= not bq_bad
print(f"peg inside the lid pocket: {pegv:.0f} mm3 of peg in the pocket (should be well above zero)")
META["standin_overlaps"] = bq

# chamber depth where ducks fall
tr = P["light_tray"]
META["depth_at_light_tray"] = round(TRAY_Z0 - Z_BACK, 2)
META["depth_elsewhere"] = round(Z_GRV0 - Z_BACK, 2)
META["duck_spare_lengthwise"] = round(TRAY_Z0 - Z_BACK - DUCK[0], 2)
META["hole"] = dict(Y=[round(v, 2) for v in HOLE_Y], Z=[round(v, 2) for v in HOLE_Z])
META["pins_to_rear_wall"] = round(pz0 - (dz0 + 1.2), 1)
META["board"] = dict(screen_face_Z=round(MASK_Z0, 2), face_to_lid_front_nominal=round(LID_Z_NOM + T_LID - MASK_Z0, 2))
print("ALL CHECKS OK" if ok else "CHECKS FAILED")

# ================================================================== export
box_dir = os.path.join(outdir, "boxframe"); os.makedirs(box_dir, exist_ok=True)
stl_dir = os.path.join(outdir, "stl"); os.makedirs(stl_dir, exist_ok=True)
for n, m in P.items():
    m.export(os.path.join(box_dir, n + ".stl"))
for n, m in Q.items():
    m.export(os.path.join(box_dir, "q_" + n + ".stl"))
for k, m in lid_at(LID_Z_NOM).items():
    m.export(os.path.join(box_dir, "lidasm_" + k + ".stl"))
ins_black.export(os.path.join(box_dir, "insert_black.stl")); ins_yellow.export(os.path.join(box_dir, "insert_yellow.stl"))
body_white.export(os.path.join(box_dir, "body_white.stl")); body_black.export(os.path.join(box_dir, "body_black.stl"))


def Rm(rows):
    M = np.eye(4); M[:3, :3] = np.array(rows, float); return M


ORIENT = {                                         # box frame -> print frame (third row = what points up off the bed)
    "insert": Rm([[0, 1, 0], [0, 0, -1], [1, 0, 0]]) if False else Rm([[0, 0, 1], [0, 1, 0], [1, 0, 0]]) @ Rm([[1, 0, 0], [0, 1, 0], [0, 0, 1]]),
    "light_tray": Rm([[1, 0, 0], [0, 1, 0], [0, 0, 1]]),                # back plate down
    "electronics_tray": Rm([[0, 0, -1], [0, 1, 0], [1, 0, 0]]),         # deck down
    "screen_cartridge": Rm([[1, 0, 0], [0, -1, 0], [0, 0, -1]]),        # mask down
    "shelf_block_L": Rm([[1, 0, 0], [0, -1, 0], [0, 0, -1]]),           # front end down
}


def to_bed(m, M=None):
    m = m.copy()
    if M is not None:
        assert abs(np.linalg.det(M[:3, :3]) - 1) < 1e-9, "orientation must be a rotation"
        m.apply_transform(M)
    m.apply_translation([-m.bounds[0][0], -m.bounds[0][1], -m.bounds[0][2]])
    return fix(m)


FILES = {}


def save(name, m, note=""):
    path = os.path.join(stl_dir, name + ".stl")
    m.export(path)
    e = m.bounds[1] - m.bounds[0]
    FILES[name] = dict(size=[round(float(v), 1) for v in e], volume_cm3=round(float(abs(m.volume)) / 1000, 1), watertight=bool(m.is_watertight), pieces=len(m.split()), note=note)


# body: two colours, same origin
b = body.copy(); off = -b.bounds[0]
for nm, m in (("01_body_black", body_black), ("01_body_white_back", body_white)):
    mm = m.copy(); mm.apply_translation(off); save(nm, mm, "load both together as one object; white = first 1 mm")
save("01_body_single_colour", to_bed(body), "same body in one piece, if you would rather add a colour change at 1 mm yourself")
# lid: four colours, same origin, back of the lid on the bed
# The lid-local frame (u across, v down, w forward) is left-handed, so it must be un-mirrored before it becomes a print file:
# x = v, y = u, z = w is the real, right-handed lid (the same swap the assembly uses).
SWAP = Rm([[0, 1, 0], [1, 0, 0], [0, 0, 1]])
LID_PRINT = Rm([[0, 1, 0], [-1, 0, 0], [0, 0, 1]]) @ SWAP          # then a quarter turn so the text reads left to right on the bed
assert abs(np.linalg.det(LID_PRINT[:3, :3]) + 1) < 1e-9             # one mirror (the un-mirroring), no more
base = lid_black.copy(); base.apply_transform(LID_PRINT); off = -base.bounds[0]
for nm, m in (("02_lid_black", lid_black), ("02_lid_red", lid_red), ("02_lid_white", lid_white), ("02_lid_yellow", lid_yellow)):
    mm = m.copy(); mm.apply_transform(LID_PRINT); mm.apply_translation(off); save(nm, mm, "load all four together as one object")
CAP_PRINT = Rm([[1, 0, 0], [0, -1, 0], [0, 0, -1]]) @ LID_PRINT    # same, then turned face down
for tag, (cc, cy, ck) in (("skin1.0", (cap10, cap10_y, cap10_k)), ("skin0.6_brighter", (cap06, cap06_y, cap06_k))):
    base = cc.copy(); base.apply_transform(CAP_PRINT); off = -base.bounds[0]
    for nm, m in ((f"03_duck_cap_{tag}_yellow", cy), (f"03_duck_cap_{tag}_black", ck)):
        mm = m.copy(); mm.apply_transform(CAP_PRINT); mm.apply_translation(off); save(nm, mm, "face down; load yellow and black together as one object")
save("04_insert", to_bed(P["insert"], Rm([[0, 1, 0], [0, 0, 1], [1, 0, 0]])), "black; top face down, no supports")
_R = Rm([[0, 1, 0], [0, 0, 1], [1, 0, 0]]); _b = P["insert"].copy(); _b.apply_transform(_R); _off = -_b.bounds[0]
save("15_funnel_liner_yellow", to_bed(ins_liner, Rm([[0, 1, 0], [0, 0, -1], [-1, 0, 0]])), "yellow; small end (hole edge) down, no supports; drops into the funnel of the insert")

save("06_tray_combined_white", to_bed(P["light_tray"], Rm([[1, 0, 0], [0, -1, 0], [0, 0, -1]])), "light tray + LED hood + electronics tray in one; front edges down")
save("07_screen_cartridge", to_bed(P["screen_cartridge"], Rm([[0, 1, 0], [1, 0, 0], [0, 0, -1]])), "window face down; print this first and test the board in it")
save("09_tray_stop", to_bed(P["tray_stop"], Rm([[0, -1, 0], [1, 0, 0], [0, 0, 1]])), "wide face down")
save("10_shelf_sensor_block_x2", to_bed(P["shelf_block_L"], Rm([[0, 1, 0], [1, 0, 0], [0, 0, -1]])), "print two; solid end down")
save("11_magnet_test_coupon", to_bed(EXTRA["magnet_test_coupon"], Rm([[1, 0, 0], [0, -1, 0], [0, 0, -1]])), "optional: recess faces up, no supports; try a magnet and screw in it before the long body print")
save("12_screw_spacer_1mm_x6", to_bed(EXTRA["spacer_1mm"]), "one under every magnet screw head")
save("12_screw_spacer_2mm_x6", to_bed(EXTRA["spacer_2mm"]), "only if the magnet screws bottom out")
from load3mf import load_3mf_dir
for oid, T, parts3 in load_3mf_dir(root3mf):
    if oid in ("16", "18"):
        t = trimesh.Trimesh(parts3[0][1], parts3[0][2], process=True)
        save("13_locking_peg_stock" if oid == "18" else "14_peg_key_stock", to_bed(from_tri(t)), "unchanged from the original model")
META["files"] = FILES
json.dump(META, open(os.path.join(outdir, "meta.json"), "w"), indent=1)
print(json.dumps(META, indent=1))
