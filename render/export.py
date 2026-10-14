import os, sys, numpy as np, trimesh
src, out = sys.argv[1:3]
YC, H = 84.375, 225.0
B = np.array([[0, 1, 0, -YC], [-1, 0, 0, H], [0, 0, 1, 0], [0, 0, 0, 1.0]])
NAMES = ["body_black", "body_white", "insert_black", "insert_yellow", "funnel_liner", "light_tray", "screen_cartridge", "tray_stop", "shelf_block_L", "shelf_block_R",
         "q_pcb", "q_oled", "q_pins", "q_battery", "q_keeper", "q_sensor_drop_L", "q_sensor_drop_R", "q_sensor_shelf", "q_leds", "q_peg", "q_magnets",
         "lidasm_lid_black", "lidasm_lid_red", "lidasm_lid_white", "lidasm_lid_yellow", "lidasm_cap_yellow", "lidasm_cap_black"]
sc = trimesh.Scene()
for n in NAMES:
    m = trimesh.load(os.path.join(src, n + ".stl")); m.merge_vertices(); m.apply_transform(B)
    if m.volume < 0: m.invert()
    print(n, np.round(m.bounds, 1).tolist())
    m.visual = trimesh.visual.ColorVisuals(m); sc.add_geometry(m, node_name=n, geom_name=n)
sc.export(out); print("wrote", out, os.path.getsize(out))
