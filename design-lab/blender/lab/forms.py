"""Slabs with pockets: the one form every lab surface is made of.

A slab is a rounded rectangle swept with an edge profile (soft round or machined chamfer)
whose flat top may hold pockets: recesses with a softly rounded lip, straight walls and
(optionally) a flat floor. The top is triangulated around the pockets, so a card can hold
its display's bezel, a button's groove or a fader's slot as real geometry, lit by the
same studio as everything else. Floors that need pockets of their own are separate slabs
placed at the pocket's depth.

Units: rects and radii in design px (geometry.PX mm each); heights, bevels, lips in mm.
"""

import bmesh
import bpy

from . import geometry


def _ring(bm, rect, radius_px, inset, z):
    cx, cy, w, h = geometry.design_rect_mm(rect)
    return [bm.verts.new((x, y, z)) for (x, y) in geometry.outline(cx, cy, w, h, radius_px * geometry.PX, inset)]


def _bridge(bm, a, b, mat, centre, outward, smooth=True):
    """Quads between two rings, each facing away from the solid: up and outwards on a
    slab's edge, up and towards the middle on a pocket's lip and walls."""
    n = len(a)
    for i in range(n):
        j = (i + 1) % n
        f = bm.faces.new((a[i], a[j], b[j], b[i]))
        f.material_index = mat
        f.smooth = smooth
        f.normal_update()
        c = f.calc_center_median()
        rx, ry = c.x - centre[0], c.y - centre[1]
        if not outward:
            rx, ry = -rx, -ry
        length = max(1e-9, (rx * rx + ry * ry) ** 0.5)
        if f.normal.x * rx / length + f.normal.y * ry / length + 0.5 * f.normal.z < 0.0:
            f.normal_flip()


def _cap(bm, rings, mat):
    """A flat face bounded by rings[0] with the other rings as holes (separate vertices, so
    it stays flat under smooth shading)."""
    edges = []
    for ring in rings:
        copy = [bm.verts.new(v.co) for v in ring]
        edges += [bm.edges.new((copy[k], copy[(k + 1) % len(copy)])) for k in range(len(copy))]
    out = bmesh.ops.triangle_fill(bm, use_beauty=True, use_dissolve=False, edges=edges, normal=(0.0, 0.0, 1.0))
    for f in out["geom"]:
        if isinstance(f, bmesh.types.BMFace):
            f.material_index = mat
            f.smooth = False
            f.normal_update()
            if f.normal.z < 0.0:
                f.normal_flip()


def edge_profile(z0, height, bevel, chamfer=False, segments=8):
    b = min(bevel, 0.95 * height)
    top = z0 + height
    prof = [(0.0, z0), (0.0, top - b)]
    if b > 0.0:
        if chamfer:
            prof.append((b, top))
        else:
            prof += geometry.arc((0.0, top - b), (b, top), (b, top - b), segments)
    else:
        prof.append((0.0, top))
    return prof


def slab(name, rect, radius_px, z0, height, bevel, material, chamfer=False, pockets=(), extra_materials=(), segments=8):
    """pockets: dicts {rect, radius, depth, lip, floor (material index or None), wall (index)}.
    Material index 0 is `material`; extra_materials follow in order."""
    bm = bmesh.new()
    prof = edge_profile(z0, height, bevel, chamfer, segments)
    rings = [_ring(bm, rect, radius_px, inset, z) for inset, z in prof]
    centre = geometry.design_rect_mm(rect)[:2]
    for a, b in zip(rings, rings[1:]):
        _bridge(bm, a, b, 0, centre, True)
    top = z0 + height
    holes = []
    for p in pockets:
        # A pocket never reaches the slab's own base: a floor there would be coplanar with
        # whatever the slab sits on (z-fighting).
        p = dict(p, depth=min(p["depth"], height - 0.05) if z0 > 0.0 else p["depth"])
        lip = min(p.get("lip", 0.0), 0.9 * p["depth"])
        pp = [(-lip, top)]
        if lip > 0.0:
            pp += geometry.arc((-lip, top), (0.0, top - lip), (-lip, top - lip), p.get("segments", 6))
        pp.append((0.0, top - p["depth"]))
        prings = [_ring(bm, p["rect"], p["radius"], inset, z) for inset, z in pp]
        pcentre = geometry.design_rect_mm(p["rect"])[:2]
        for a, b in zip(prings, prings[1:]):
            _bridge(bm, a, b, p.get("wall", 0), pcentre, False)
        holes.append(prings[0])
        if p.get("floor") is not None:
            _cap(bm, [prings[-1]], p["floor"])
    _cap(bm, [rings[-1]] + holes, 0)
    mesh = bpy.data.meshes.new(name)
    bm.normal_update()
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    for m in (material,) + tuple(extra_materials):
        obj.data.materials.append(m)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def ring(name, rect, radius_px, z0, height, width_px, material):
    """A thin band along a rounded rectangle's outline (an inlay, a lit edge)."""
    inner = (rect[0] + width_px, rect[1] + width_px, rect[2] - 2 * width_px, rect[3] - 2 * width_px)
    return slab(name, rect, radius_px, z0, height, 0.0, material,
                pockets=[{"rect": inner, "radius": max(0.5, radius_px - width_px), "depth": height + 0.001, "lip": 0.0}])


def dome(name, centre, diameter_px, z0, height, material):
    """A small lens (an LED): a disc whose edge rounds over almost to the top."""
    r = 0.5 * diameter_px
    return slab(name, (centre[0] - r, centre[1] - r, diameter_px, diameter_px), r, z0, height, 0.92 * height, material, segments=10)


def expand(rect, d):
    return (rect[0] - d, rect[1] - d, rect[2] + 2 * d, rect[3] + 2 * d)
