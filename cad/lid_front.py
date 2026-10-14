"""Front view of the stock lid, in lid-local mm: u = across (left to right), v = down from the lid's top edge."""
import sys, os, json
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from load3mf import load_3mf_dir


def lid_parts(root):
    items = {oid: parts for oid, T, parts in load_3mf_dir(root)}
    parts = items["14"]
    plate = [p for p in parts if p[0] == "3"][0]
    mn = plate[1].min(0)
    out = {}
    for cid, v, f, _ in parts:
        w = v - mn
        # printed X runs down the lid, printed Y runs across it
        uvz = np.c_[w[:, 1], w[:, 0], w[:, 2]]
        out[cid] = (uvz, f)
    return out


def top_polys(uvz, f):
    tri = uvz[f]
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    up = n[:, 2] > 1e-9
    return tri[up][:, :, :2]


if __name__ == "__main__":
    root, png = sys.argv[1], sys.argv[2]
    parts = lid_parts(root)
    names = {"3": "plate", "4": "duck", "5": "drop text", "6": "take text", "13": "enter text"}
    fig, ax = plt.subplots(figsize=(7, 7.2))
    cols = {"3": "#7E00DE", "13": "#000000"}
    for cid, (uvz, f) in parts.items():
        ax.add_collection(PolyCollection(top_polys(uvz, f), facecolors=cols.get(cid, "#F4EE2A"), edgecolors="none"))
        b0, b1 = uvz.min(0), uvz.max(0)
        print(cid, names.get(cid, "arrow"), "u", round(b0[0], 2), round(b1[0], 2), "v", round(b0[1], 2), round(b1[1], 2), "z", round(b0[2], 2), round(b1[2], 2))
    ax.set_xlim(-2, 158); ax.set_ylim(162, -2); ax.set_aspect("equal")
    fig.savefig(png, dpi=70)
