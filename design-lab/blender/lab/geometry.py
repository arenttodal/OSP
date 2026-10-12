"""Parametric geometry for the lab: rounded-rectangle forms swept with an edge profile.

Everything is built from one primitive. A rounded-rectangle outline is offset inwards by
`inset` at each profile point (inset, z), and consecutive offsets are joined into quads, so
edges are real geometry: a soft bevel, a machined chamfer, the lip of a pocket. Flat faces
(tops, pocket floors) are separate vertices, so they stay perfectly flat under smooth
shading.

Units: design pixels in, millimetres out (PX mm per pixel, the approved knob's scale).
Design y runs down; scene y runs up.
"""

import math

import bpy

PX = 0.31          # mm per design pixel (the knob's 93 px body is 28.9 mm)
CORNER_SEGMENTS = 18


def outline(cx, cy, w, h, radius, inset):
    """Points of a rounded rectangle (centre cx, cy; size w x h; mm) shrunk by `inset`."""
    # Never a fully round end: a zero-length straight would duplicate vertices (and the
    # cap triangulation drops a hole with duplicates).
    r0 = min(radius, 0.5 * min(w, h) - 0.01)
    r = max(0.02, r0 - inset)
    hw, hh = 0.5 * w - inset, 0.5 * h - inset
    pts = []
    corners = [(hw - r, hh - r, 0.0), (-hw + r, hh - r, 0.5 * math.pi), (-hw + r, -hh + r, math.pi), (hw - r, -hh + r, 1.5 * math.pi)]
    for ccx, ccy, a0 in corners:
        for k in range(CORNER_SEGMENTS + 1):
            a = a0 + 0.5 * math.pi * k / CORNER_SEGMENTS
            pts.append((cx + ccx + r * math.cos(a), cy + ccy + r * math.sin(a)))
    return pts


def arc(p0, p1, centre, n):
    """Points from p0 to p1 around centre (profile space: inset, z)."""
    a0 = math.atan2(p0[1] - centre[1], p0[0] - centre[0])
    a1 = math.atan2(p1[1] - centre[1], p1[0] - centre[0])
    d = a1 - a0
    while d > math.pi:
        d -= 2 * math.pi
    while d < -math.pi:
        d += 2 * math.pi
    rad = math.hypot(p0[0] - centre[0], p0[1] - centre[1])
    return [(centre[0] + rad * math.cos(a0 + d * k / n), centre[1] + rad * math.sin(a0 + d * k / n)) for k in range(1, n + 1)]


def design_rect_mm(rect):
    """(x, y, w, h) in design px -> (cx, cy, w, h) in mm, scene y up."""
    x, y, w, h = rect
    return ((x + 0.5 * w) * PX, -(y + 0.5 * h) * PX, w * PX, h * PX)


def swept(name, rect, radius_px, profile, material, caps=(), smooth=True):
    """profile: [(inset_mm, z_mm)], outside to inside. caps: [(index of the profile point
    whose ring is closed by a flat face)]."""
    cx, cy, w, h = design_rect_mm(rect)
    radius = radius_px * PX
    verts, faces = [], []
    rings = []
    for inset, z in profile:
        ring = []
        for (px, py) in outline(cx, cy, w, h, radius, inset):
            ring.append(len(verts))
            verts.append((px, py, z))
        rings.append(ring)
    n = len(rings[0])
    for a, b in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            faces.append((a[i], a[j], b[j], b[i]))
    flat = []
    for c in caps:
        start = len(verts)
        for idx in rings[c]:
            verts.append(verts[idx])
        flat.append(len(faces))
        faces.append(tuple(range(start, start + n)))
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces)
    mesh.validate()
    smooth_flags = [smooth] * len(mesh.polygons)
    for f in flat:
        smooth_flags[f] = False
    mesh.polygons.foreach_set("use_smooth", smooth_flags)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    obj.data.materials.append(material)
    bpy.context.scene.collection.objects.link(obj)
    # Faces must point outwards / up for shading and the camera.
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    return obj


def raised(name, rect, radius_px, height, bevel, material, z0=0.0, chamfer=False, segments=8):
    """A raised slab (button, card, panel, cap): straight sides, a soft round (or machined
    chamfer) top edge, a flat top."""
    b = min(bevel, 0.95 * height)
    prof = [(0.0, z0), (0.0, z0 + height - b)]
    if b > 0.0:
        if chamfer:
            prof.append((b, z0 + height))
        else:
            prof += arc((0.0, z0 + height - b), (b, z0 + height), (b, z0 + height - b), segments)
    else:
        prof.append((0.0, z0 + height))
    return swept(name, rect, radius_px, prof, material, caps=(len(prof) - 1,))


def pocket(name, rect, radius_px, depth, lip, margin, material, floor_material=None, z0=0.0, segments=6):
    """A pocket cut into a surface: a flat collar (the surface, `margin` mm wide) round a
    softly rounded lip, walls down to a flat floor. floor_material: a different floor."""
    l = min(lip, 0.9 * depth)
    prof = [(-margin, z0), (-l, z0)]
    prof += arc((-l, z0), (0.0, z0 - l), (-l, z0 - l), segments)
    prof.append((0.0, z0 - depth))
    obj = swept(name, rect, radius_px, prof, material)
    if floor_material is None:
        floor_material = material
    floor = raised(name + "-floor", rect, radius_px, 0.0001, 0.0, floor_material, z0=z0 - depth - 0.0001)
    return obj, floor


def plane(name, rect, material, z=0.0):
    cx, cy, w, h = design_rect_mm(rect)
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata([(cx - w / 2, cy - h / 2, z), (cx + w / 2, cy - h / 2, z), (cx + w / 2, cy + h / 2, z), (cx - w / 2, cy + h / 2, z)], [], [(0, 1, 2, 3)])
    obj = bpy.data.objects.new(name, mesh)
    obj.data.materials.append(material)
    bpy.context.scene.collection.objects.link(obj)
    return obj
