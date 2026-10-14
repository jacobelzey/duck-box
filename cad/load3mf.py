"""Minimal loader for Bambu/production-extension 3MF files.

Returns, per build item, a list of (component_objectid, vertices, faces) with
all transforms applied (component transform then build-item transform), so
coordinates are in printed millimetres.
"""
import os
import sys
import numpy as np
from lxml import etree

NS = {"m": "http://schemas.microsoft.com/3dmanufacturing/core/2015/02",
      "p": "http://schemas.microsoft.com/3dmanufacturing/production/2015/06"}
PATH_ATTR = "{%s}path" % NS["p"]


def parse_transform(s):
    """3MF transform: 12 numbers, row-major 4x3 (row-vector convention)."""
    M = np.eye(4)
    if not s:
        return M
    v = [float(x) for x in s.split()]
    # v = m00 m01 m02 m10 m11 m12 m20 m21 m22 m30 m31 m32 ; p' = p * M (row vec)
    A = np.array(v[:9]).reshape(3, 3)
    t = np.array(v[9:12])
    M[:3, :3] = A.T
    M[:3, 3] = t
    return M


def _safe_parser():
    return etree.XMLParser(resolve_entities=False, no_network=True, huge_tree=True)


def load_meshes(model_path):
    """Return {objectid: (verts Nx3, faces Mx3)} for mesh objects in a .model file."""
    tree = etree.parse(model_path, _safe_parser())
    out = {}
    for obj in tree.getroot().iterfind(".//m:object", NS):
        mesh = obj.find("m:mesh", NS)
        if mesh is None:
            continue
        vs = np.array([[float(v.get("x")), float(v.get("y")), float(v.get("z"))]
                       for v in mesh.iterfind("m:vertices/m:vertex", NS)])
        fs = np.array([[int(t.get("v1")), int(t.get("v2")), int(t.get("v3"))]
                       for t in mesh.iterfind("m:triangles/m:triangle", NS)], dtype=np.int64)
        out[obj.get("id")] = (vs, fs)
    return out


def load_3mf_dir(root):
    main = os.path.join(root, "3D", "3dmodel.model")
    tree = etree.parse(main, _safe_parser())
    r = tree.getroot()
    cache = {}
    objects = {}
    for obj in r.iterfind("m:resources/m:object", NS):
        comps = []
        for c in obj.iterfind("m:components/m:component", NS):
            comps.append((c.get(PATH_ATTR), c.get("objectid"), parse_transform(c.get("transform"))))
        objects[obj.get("id")] = comps
    items = []
    for it in r.iterfind("m:build/m:item", NS):
        oid = it.get("objectid")
        T = parse_transform(it.get("transform"))
        parts = []
        for path, cid, Tc in objects[oid]:
            # confine to the extracted directory
            full = os.path.normpath(os.path.join(root, path.lstrip("/")))
            if not full.startswith(os.path.normpath(root) + os.sep):
                raise ValueError("component path escapes archive: %r" % path)
            if full not in cache:
                cache[full] = load_meshes(full)
            vs, fs = cache[full][cid]
            M = T @ Tc
            v4 = np.c_[vs, np.ones(len(vs))] @ M.T
            parts.append((cid, v4[:, :3], fs, Tc))
        items.append((oid, T, parts))
    return items


if __name__ == "__main__":
    root = sys.argv[1]
    np.set_printoptions(precision=3, suppress=True)
    for oid, T, parts in load_3mf_dir(root):
        allv = np.vstack([p[1] for p in parts])
        mn, mx = allv.min(0), allv.max(0)
        print(f"build item obj {oid}: {len(parts)} part(s)  size(mm) = {np.round(mx-mn,2)}  min={np.round(mn,2)} max={np.round(mx,2)}")
        for cid, v, f, Tc in parts:
            print(f"    comp {cid}: verts {len(v)} tris {len(f)} size {np.round(v.max(0)-v.min(0),2)} min {np.round(v.min(0),2)}")
