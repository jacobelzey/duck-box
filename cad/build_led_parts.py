"""Build the LED add-on parts for the Drop-a-Duck box.

Frame used everywhere here ("box frame", printed millimetres, min corner at 0):
  X = 0 at the top of the box, 225 at the bottom
  Y = across the width (0..168.75)
  Z = 0 at the back face (against the door), 87.5 at the front
"""
import os
import sys
import json
import numpy as np
import trimesh

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from load3mf import load_3mf_dir

root, outdir = sys.argv[1], sys.argv[2]
np.set_printoptions(precision=3, suppress=True, linewidth=200)

# ------------------------------------------------------------------ load box
items = {oid: (T, parts) for oid, T, parts in load_3mf_dir(root)}
T_build, parts = items["2"]
cid, v_print, faces, _ = parts[0]
box_min = v_print.min(0)
box = trimesh.Trimesh(v_print - box_min, faces, process=False)
assert box.is_watertight
SCALE = T_build[0, 0]
assert abs(SCALE - 1.25) < 1e-9, SCALE


def face_level(mesh, axis, lo, hi, sign):
    """Coordinates of large flat faces whose normal is +/-axis, between lo and hi."""
    n = mesh.face_normals[:, axis]
    c = mesh.triangles_center[:, axis]
    sel = (n * sign > 0.999) & (c > lo) & (c < hi)
    # group by 0.001 mm, but report the exact (unrounded) plane coordinate
    groups, areas = {}, mesh.area_faces
    for val, a in zip(c[sel], areas[sel]):
        g = groups.setdefault(round(float(val), 3), [0.0, 0.0])
        g[0] += a; g[1] += a * float(val)
    res = [(g[1] / g[0], g[0]) for g in groups.values()]
    return sorted(res, key=lambda kv: -kv[1])


# measured reference planes (largest-area faces win)
DIV_TOP = face_level(box, 0, 150, 160, -1)[0][0]     # floor of upper chamber (normal -X)
DIV_BOT = face_level(box, 0, 158, 165, +1)[0][0]     # ceiling of shelf (normal +X)
SHELF_FLOOR = face_level(box, 0, 210, 222, -1)[0][0]
Y_LO = face_level(box, 1, 6, 12, +1)[0][0]           # inner face of left wall
Y_HI = face_level(box, 1, 155, 162, -1)[0][0]        # inner face of right wall
Z_BACK = face_level(box, 2, 3, 8, +1)[0][0]          # inner face of back panel
zs = np.unique(np.round(box.vertices[:, 2], 3))
Z_FRONT = float(zs[np.argmin(np.abs(zs - 73.75))])   # back of the lid groove
assert abs(Z_FRONT - 73.75) < 0.05, zs
print("divider top/bottom X:", DIV_TOP, DIV_BOT, " shelf floor X:", SHELF_FLOOR)
print("chamber Y:", Y_LO, Y_HI, " back Z:", Z_BACK, " lid-groove Z:", Z_FRONT)
print("chamber inside W x D:", round(Y_HI - Y_LO, 2), "x", round(Z_FRONT - Z_BACK, 2),
      " shelf opening H:", round(SHELF_FLOOR - DIV_BOT, 2), " divider thickness:", round(DIV_BOT - DIV_TOP, 2))

# ------------------------------------------------------------- design values
P = dict(
    led_hole_d=5.2,            # for a 5 mm LED body; flange (5.9) stays on top
    led_z=66.0,                # distance from the back face of the box
    led_pitch=50.0,            # three LEDs centred on the shelf
    bay_h=23.0,                # clear height under the deck (3xAA holder is ~20)
    deck_t=2.0,
    skirt_t=2.0,
    side_gap=0.5,              # deck to side walls, each side
    back_gap=0.5,
    front_gap=0.75,            # deck to the lid groove plane
    win_len=24.0, win_wid=16.0,  # switch / finger window (obround)
    win_y_off=22.0,            # window centre offset from box centre line, +Y
    win_z=40.0,
)
yc = 0.5 * (Y_LO + Y_HI)
led_y = [yc - P["led_pitch"], yc, yc + P["led_pitch"]]
P["led_y"] = [round(float(y), 3) for y in led_y]
P["ref"] = dict(DIV_TOP=float(DIV_TOP), DIV_BOT=float(DIV_BOT), SHELF_FLOOR=float(SHELF_FLOOR),
                Y_LO=float(Y_LO), Y_HI=float(Y_HI), Z_BACK=float(Z_BACK), Z_FRONT=float(Z_FRONT))


def cyl_x(d, x0, x1, y, z, sections=64):
    c = trimesh.creation.cylinder(radius=d / 2, height=x1 - x0, sections=sections)
    c.apply_transform(trimesh.transformations.rotation_matrix(np.pi / 2, [0, 1, 0]))
    c.apply_translation([(x0 + x1) / 2, y, z])
    return c


def boxm(x0, x1, y0, y1, z0, z1):
    b = trimesh.creation.box(extents=[x1 - x0, y1 - y0, z1 - z0])
    b.apply_translation([(x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2])
    return b


# ------------------------------------------------------- 1) LED holes in box
drills = [cyl_x(P["led_hole_d"], DIV_TOP - 1.5, DIV_BOT + 1.5, y, P["led_z"]) for y in led_y]
box_led = trimesh.boolean.difference([box] + drills, engine="manifold")
assert box_led.is_watertight
removed = box.volume - box_led.volume
expect = 3 * np.pi * (P["led_hole_d"] / 2) ** 2 * (DIV_BOT - DIV_TOP)
print(f"holes: removed {removed:.1f} mm3, expected ~{expect:.1f} mm3 (64-gon slightly less)")
assert abs(removed - expect) / expect < 0.01

# -------------------------------------------------------------- 2) the deck
dx1 = DIV_TOP                      # skirts stand on the divider
dx0 = DIV_TOP - P["bay_h"] - P["deck_t"]   # top surface of the deck
y0, y1 = Y_LO + P["side_gap"], Y_HI - P["side_gap"]
z0, z1 = Z_BACK + P["back_gap"], Z_FRONT - P["front_gap"]
plate = boxm(dx0, dx0 + P["deck_t"], y0, y1, z0, z1)
skirt_l = boxm(dx0, dx1, y0, y0 + P["skirt_t"], z0, z1)
skirt_r = boxm(dx0, dx1, y1 - P["skirt_t"], y1, z0, z1)
skirt_b = boxm(dx0, dx1, y0, y1, z0, z0 + P["skirt_t"])
deck = trimesh.boolean.union([plate, skirt_l, skirt_r, skirt_b], engine="manifold")
# obround window through the plate
wy, wz = yc + P["win_y_off"], P["win_z"]
r = P["win_wid"] / 2
half = P["win_len"] / 2 - r
win = trimesh.boolean.union([
    boxm(dx0 - 1, dx0 + P["deck_t"] + 1, wy - half, wy + half, wz - r, wz + r),
    cyl_x(P["win_wid"], dx0 - 1, dx0 + P["deck_t"] + 1, wy - half, wz),
    cyl_x(P["win_wid"], dx0 - 1, dx0 + P["deck_t"] + 1, wy + half, wz),
], engine="manifold")
deck = trimesh.boolean.difference([deck, win], engine="manifold")
assert deck.is_watertight
P["deck"] = dict(x_top=float(dx0), x_bottom=float(dx1), y0=float(y0), y1=float(y1), z0=float(z0), z1=float(z1),
                 win_y=float(wy), win_z=float(wz))
print("deck extents (X,Y,Z):", deck.extents, " volume cm3:", round(deck.volume / 1000, 2))

# ------------------------------------------------------------------ exports
box_led.export(os.path.join(outdir, "work_box_led_boxframe.stl"))
deck.export(os.path.join(outdir, "work_deck_boxframe.stl"))
box.export(os.path.join(outdir, "work_box_orig_boxframe.stl"))

# print-orientation STLs
# box: already in its print orientation (back on the bed, Z up)
b = box_led.copy()
b.apply_translation(-b.bounds[0])
b.export(os.path.join(outdir, "DuckBox_body_LED_holes.stl"))
# deck: plate down on the bed, skirts up -> map box-frame X to print Z
d = deck.copy()
R = np.array([[0, 1, 0, 0], [0, 0, 1, 0], [1, 0, 0, 0], [0, 0, 0, 1.0]])  # (x,y,z)->(y,z,x)
d.apply_transform(R)
d.apply_translation(-d.bounds[0])
assert d.is_watertight and d.volume > 0
d.export(os.path.join(outdir, "LED_wiring_deck.stl"))
print("deck print size:", d.extents)

json.dump(P, open(os.path.join(outdir, "design_params.json"), "w"), indent=1)
json.dump(dict(box_min=box_min.tolist(), T_build=T_build.tolist()),
          open(os.path.join(outdir, "box_frame.json"), "w"))
print(json.dumps(P, indent=1))
