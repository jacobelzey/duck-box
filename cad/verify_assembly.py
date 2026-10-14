"""Can each part be moved into place?  Sweep it along its insertion path (in the order of the guide) and look for collisions
with everything already in the box.  Paths are lists of straight legs, walked backwards from the seated position."""
import sys, os, json
import numpy as np, trimesh, manifold3d as m3
out = sys.argv[1]
bf = os.path.join(out, "boxframe")
meta = json.load(open(os.path.join(out, "meta.json")))
def M(n):
    t = trimesh.load(os.path.join(bf, n + ".stl")); t.merge_vertices()
    ps = t.split(only_watertight=True)
    ms = [m3.Manifold(m3.Mesh64(vert_properties=np.asarray(q.vertices, np.float64), tri_verts=np.asarray(q.faces, np.uint64))) for q in ps]
    return m3.Manifold.batch_boolean(ms, m3.OpType.Add) if len(ms) > 1 else ms[0]
names = ["body", "insert", "tray_stop", "light_tray", "screen_cartridge", "shelf_block_L", "shelf_block_R",
         "q_pcb", "q_oled", "q_pins", "q_plugs", "q_battery", "q_leds", "q_sensor_drop_L", "q_sensor_drop_R", "q_keeper", "q_sensor_shelf", "q_peg", "q_magnets",
         "lidasm_lid_black", "lidasm_lid_red", "lidasm_lid_white", "lidasm_lid_yellow", "lidasm_cap_yellow", "lidasm_cap_black"]
P = {n: M(n) for n in names}
def uni(ns):
    return m3.Manifold.batch_boolean([P[n] for n in ns], m3.OpType.Add) if len(ns) > 1 else P[ns[0]]
def sweep(movers, legs, present, step=0.5):
    mover, env = uni(movers), uni(present)
    worst, pos, travelled = (0.0, 0.0), np.zeros(3), 0.0
    for d, dist in legs:
        d = np.array(d, float)
        for t in np.arange(0.0, dist + 1e-9, step):
            q = pos + d * t
            v = (mover.translate(list(q)) ^ env).volume()
            if v > worst[0]:
                worst = (v, travelled + t)
        pos = pos + d * dist; travelled += dist
    return worst
TRAY_LIFT = meta.get("tray_lift", 6.6)
LID = ["lidasm_lid_black", "lidasm_lid_red", "lidasm_lid_white", "lidasm_lid_yellow", "lidasm_cap_yellow", "lidasm_cap_black"]
SENS = ["q_sensor_drop_L", "q_sensor_drop_R"]
SENSK = SENS + ["q_keeper"]
steps = [
    ("combined tray (light tray + hood + electronics tray) + tray stop + LEDs slides in from the front", ["light_tray", "tray_stop", "q_leds"], [([0, 0, 1], 90)], ["body", "q_magnets"], 1.0),
    ("shelf block + sensor slides in from the front (crush ribs excepted)", ["shelf_block_L", "q_sensor_shelf"], [([0, 0, 1], 40)], ["body", "light_tray"], 30.0),
    ("screen cartridge + board + plugs slides in from the front", ["screen_cartridge", "q_pcb", "q_oled", "q_pins", "q_plugs"], [([0, 0, 1], 60)], ["body", "light_tray"], 1.5),
    ("battery holder slides in from the front, wire end first", ["q_battery"], [([0, 0, 1], 90)], ["body", "light_tray", "screen_cartridge"], 1.5),
    ("left drop sensor slides into its pocket from the front (crush ribs excepted)", ["q_sensor_drop_L"], [([0, 0, 1], 30)], ["insert"], 4.5),
    ("right drop sensor slides into its pocket from the front (crush ribs excepted)", ["q_sensor_drop_R"], [([0, 0, 1], 30)], ["insert"], 4.5),
    ("insert with both sensors drops in from the top (crush ribs excepted)", ["insert"] + SENSK, [([-1, 0, 0], 40)], ["body", "tray_stop", "light_tray", "q_magnets"], 4.5),
    ("lid with all its artwork slides down from the top", LID, [([-1, 0, 0], 170)], ["body", "insert", "tray_stop", "light_tray", "screen_cartridge", "q_battery"], 1.0),
    ("locking peg goes in from the back", ["q_peg"], [([0, 0, -1], 90)], ["body", "insert", "tray_stop", "light_tray", "q_magnets"] + SENSK, 1.0),
]
ok = True
for label, movers, legs, present, tol in steps:
    v, t = sweep(movers, legs, present)
    good = v <= tol
    ok &= good
    print(f"{'ok  ' if good else 'JAM '} {label}: worst overlap {v:.2f} mm3, {t:.1f} mm from home")
def held(what, movers, d, dist, against, need=0.5):
    global ok
    v = (uni(movers).translate(list(np.array(d, float) * dist)) ^ uni(against)).volume()
    print(f"     {what}: overlap {v:.2f} mm3 ({'held' if v > need else 'FREE'})"); ok &= v > need
for d, lab in (([0, 0, 1], "forward"), ([-1, 0, 0], "up"), ([1, 0, 0], "down")):
    held(f"tray stop pushed 1.5 mm {lab} (tray)", ["tray_stop"], d, 1.5, ["light_tray"])
held("tray stop pushed 1 mm back, insert not yet in (its own crush ribs)", ["tray_stop"], [0, 0, -1], 1.0, ["light_tray"], 0.2)
held("tray stop pushed 1.5 mm back (tab on the insert)", ["tray_stop"], [0, 0, -1], 1.5, ["insert"])
held("board pushed 2.5 mm backward (cartridge ledges)", ["q_pins"], [0, 0, -1], 2.5, ["screen_cartridge"], 0.05)
for sn in SENS:
    for d, lab in (([0, 1, 0], "towards +Y"), ([0, -1, 0], "towards -Y"), ([1, 0, 0], "down"), ([-1, 0, 0], "up"), ([0, 0, -1], "back")):
        held(f"{sn} pushed 2 mm {lab} (pocket)", [sn], d, 2.0, ["insert"], 2.5)
    held(f"{sn} pushed 1 mm forward (filament pin)", [sn], [0, 0, 1], 1.0, ["q_keeper"], 0.5)
v = (P["q_keeper"] ^ uni(["insert"])).volume()
print(f"     filament pins against the insert: overlap {v:.3f} mm3 (should be about zero: they sit in their holes)"); ok &= v < 0.3
for sn in SENS:
    v = (P["q_keeper"] ^ P[sn]).volume(); print(f"     filament pins against {sn}, seated: overlap {v:.3f} mm3"); ok &= v < 0.01
# the beam must have a clear path from bulb to bulb
a, b = P["q_sensor_drop_L"].bounding_box(), P["q_sensor_drop_R"].bounding_box()
bz = meta["drop_beam"]["Z"]; bxx = meta["drop_beam"]["X_from_top"]
ray = m3.Manifold.cube([2.0, b[1] - a[4], 2.0]).translate([bxx - 1.0, a[4], bz - 1.0])
v = (ray ^ uni(["insert", "body", "light_tray", "tray_stop"])).volume()
print(f"     drop beam path (2 x 2 mm, bulb tip to bulb tip, {b[1] - a[4]:.1f} mm): blocked volume {v:.3f} mm3"); ok &= v < 0.01
print("ASSEMBLY SWEEP", "OK" if ok else "FAILED")
