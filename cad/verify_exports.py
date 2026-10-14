"""Independent check of the print files: every one must map onto its place in the assembled box by a pure rotation + shift
(no mirror), and the lid must read the right way round when seen from the front."""
import sys, os, itertools
import numpy as np, trimesh
out = sys.argv[1]
stl, bf = os.path.join(out, "stl"), os.path.join(out, "boxframe")
PAIRS = {"02_lid_black": "lidasm_lid_black", "02_lid_red": "lidasm_lid_red", "03_duck_cap_skin1.0_yellow": "lidasm_cap_yellow", "04_insert": "insert",
         "06_tray_combined_white": "light_tray", "07_screen_cartridge": "screen_cartridge", "09_tray_stop": "tray_stop",
         "10_shelf_sensor_block_x2": "shelf_block_L", "01_body_single_colour": "body"}
ROTS = []
for perm in itertools.permutations(range(3)):
    for signs in itertools.product((1, -1), repeat=3):
        R = np.zeros((3, 3))
        for i, (p, s) in enumerate(zip(perm, signs)):
            R[i, p] = s
        ROTS.append(R)
ok = True
for a, b in PAIRS.items():
    A = trimesh.load(os.path.join(stl, a + ".stl")); B = trimesh.load(os.path.join(bf, b + ".stl"))
    A.merge_vertices(); B.merge_vertices()
    found = []
    for R in ROTS:
        v = A.vertices @ R.T
        v = v - v.min(0) + B.bounds[0]
        if not np.allclose(np.sort(v.max(0) - v.min(0)), np.sort(B.bounds[1] - B.bounds[0]), atol=1e-3) or not np.allclose(v.max(0), B.bounds[1], atol=1e-3):
            continue
        T = trimesh.Trimesh(v, A.faces if np.linalg.det(R) > 0 else A.faces[:, ::-1], process=False)
        d = trimesh.proximity.closest_point(B, T.vertices[:: max(1, len(T.vertices) // 400)])[1].max()
        d2 = trimesh.proximity.closest_point(T, B.vertices[:: max(1, len(B.vertices) // 400)])[1].max()
        if max(d, d2) < 0.01:
            found.append(round(float(np.linalg.det(R))))
    verdict = "rotation only" if 1 in found else ("MIRRORED" if found else "NO MATCH")
    if 1 not in found:
        ok = False
    # a part with a mirror plane of its own also matches with det -1; that is fine as long as +1 matches
    print(f"{a:34s} -> {b:20s} {verdict}  (matching orientations: +1 x{found.count(1)}, -1 x{found.count(-1)})")
print("EXPORT CHECK", "OK" if ok else "FAILED")
