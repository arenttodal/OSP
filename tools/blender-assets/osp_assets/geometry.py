"""Procedural knob geometry: a profile of revolution with flutes, built as one mesh.

The profile runs from the base ring's outer foot, over the base ring, through the small
groove that separates the cap from it, up the cap's tapered sidewall, round the soft bevel
and across the gently domed top to the centre. Every corner is filleted (molded parts have
no razor edges). Flutes are a radial displacement of the sidewall that fades in above the
groove and out across the bevel, so the top outline is only gently scalloped.

Units are millimetres here; the scene scales the object to metres.
"""

import math


def _fillet(points, radii, segments):
    """A polyline with each interior corner replaced by a circular arc of the given radius.

    points: [(r, z, region)], radii: per point (0: keep the corner), segments: per point.
    The arc's points carry the corner's region with a parameter t (0..1 along the arc).
    """
    out = [(points[0][0], points[0][1], points[0][2], 0.0)]
    for i in range(1, len(points) - 1):
        (ax, az, _), (px, pz, region), (bx, bz, _) = points[i - 1], points[i], points[i + 1]
        rho = radii[i]
        if rho <= 0.0:
            out.append((px, pz, region, 0.0))
            continue
        ux, uz = ax - px, az - pz
        vx, vz = bx - px, bz - pz
        lu, lv = math.hypot(ux, uz), math.hypot(vx, vz)
        ux, uz, vx, vz = ux / lu, uz / lu, vx / lv, vz / lv
        cos_theta = max(-1.0, min(1.0, ux * vx + uz * vz))
        theta = math.acos(cos_theta)                      # interior angle at the corner
        if theta < 1e-4 or abs(theta - math.pi) < 1e-4:
            out.append((px, pz, region, 0.0))
            continue
        d = rho / math.tan(0.5 * theta)
        d = min(d, 0.49 * lu, 0.49 * lv)                  # never past half of either edge
        rho = d * math.tan(0.5 * theta)
        t0 = (px + ux * d, pz + uz * d)
        t1 = (px + vx * d, pz + vz * d)
        # The centre lies along the bisector at rho / sin(theta / 2).
        bx_, bz_ = ux + vx, uz + vz
        lb = math.hypot(bx_, bz_)
        h = rho / math.sin(0.5 * theta)
        cx, cz = px + bx_ / lb * h, pz + bz_ / lb * h
        a0 = math.atan2(t0[1] - cz, t0[0] - cx)
        a1 = math.atan2(t1[1] - cz, t1[0] - cx)
        delta = a1 - a0
        while delta > math.pi:
            delta -= 2.0 * math.pi
        while delta < -math.pi:
            delta += 2.0 * math.pi
        n = max(2, segments[i])
        for k in range(n + 1):
            a = a0 + delta * k / n
            out.append((cx + rho * math.cos(a), cz + rho * math.sin(a), region, k / n))
    last = points[-1]
    out.append((last[0], last[1], last[2], 1.0))
    return out


def _subdivide(profile, max_length):
    """Split long straight runs so fades along them stay smooth."""
    out = [profile[0]]
    for (r0, z0, g0, t0), (r1, z1, g1, t1) in zip(profile, profile[1:]):
        length = math.hypot(r1 - r0, z1 - z0)
        n = max(1, int(math.ceil(length / max_length)))
        for k in range(1, n + 1):
            f = k / n
            out.append((r0 + (r1 - r0) * f, z0 + (z1 - z0) * f, g1 if k == n else g0, t1 if k == n else (t0 if g0 == g1 else 0.0)))
    return out


def smoothstep(e0, e1, x):
    if e1 == e0:
        return 1.0 if x >= e1 else 0.0
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def knob_profile(g):
    """[(r, z, region, t)] from the outer foot to the top centre, in mm."""
    rb = 0.5 * g["diameter"]                      # the cap's radius at its foot
    rt = rb * (1.0 - g["taper"])                  # ... and at its top edge
    height = g["height"]
    dome = g["topCurvature"]
    edge_z = height - dome                        # the top face meets the bevel here
    bevel = g["bevelWidth"]
    bevel_segments = int(g["bevelSegments"])
    small = g.get("edgeRound", 0.25)
    sk = g["skirt"]

    corners, radii, segs = [], [], []

    def corner(r, z, region, radius=0.0, n=4):
        corners.append((r, z, region))
        radii.append(radius)
        segs.append(n)

    if sk.get("enabled", False):
        rs = rb + sk["extraRadius"]
        hs = sk["height"]
        rg = rb - sk["gapDepth"]
        zb = hs + sk["gap"]
        corner(rs, 0.0, "skirt")
        corner(rs, hs, "skirt", sk.get("round", 0.3), 5)
        corner(rg, hs, "groove", 0.08, 2)
        corner(rg, zb, "groove", 0.08, 2)
        corner(rb, zb, "side", small, 4)
    else:
        zb = 0.0
        corner(rb, 0.0, "side")

    # The top face near the rim, as a straight line along the dome's tangent: the bevel is
    # a fillet between the sidewall and that tangent; the dome replaces the line after it.
    slope = -2.0 * dome / rt                       # dz/dr of the dome at the rim
    probe_r = rt - 3.0 * bevel
    corner(rt, edge_z, "bevel", bevel, bevel_segments * 2)
    corner(probe_r, edge_z + slope * (probe_r - rt), "top")

    profile = _fillet(corners, radii, segs)
    profile = profile[:-1]                         # drop the tangent probe; the dome follows
    profile = _subdivide(profile, 0.25)

    # The dome from the bevel's inner tangent point to the centre (denser near the rim).
    r_start, z_start = profile[-1][0], profile[-1][1]
    rings = 36
    for k in range(1, rings + 1):
        f = k / rings
        r = r_start * (1.0 - f) ** 1.25
        z = edge_z + dome * (1.0 - (r / rt) ** 2)
        # Join the bevel's end height smoothly (it sits on the tangent line, a hair off).
        z += (z_start - (edge_z + dome * (1.0 - (r_start / rt) ** 2))) * (1.0 - smoothstep(0.0, 0.25, f))
        profile.append((r, z, "top", f))
    return profile, {"rb": rb, "rt": rt, "zb": zb, "edgeZ": edge_z, "height": height,
                     "outer": rb + (sk["extraRadius"] if sk.get("enabled", False) else 0.0)}


def flute_weight(point, info, g):
    """How much of the flutes' depth this profile point carries (0..1)."""
    r, z, region, t = point
    fl = g["flutes"]
    if region in ("skirt", "groove"):
        return 0.0
    if region == "side":
        return smoothstep(info["zb"], info["zb"] + fl.get("fadeIn", 0.5), z)
    if region == "bevel":
        return 1.0 - (1.0 - fl.get("topFade", 0.5)) * smoothstep(0.0, 1.0, t)
    # The top face: what is left fades out within about a millimetre of the rim.
    inner = info["rt"] - g["bevelWidth"] - 1.0
    return fl.get("topFade", 0.5) * smoothstep(inner, info["rt"] - g["bevelWidth"], r)


def flute_shape(phi, g):
    """0 on a rib's crest, 1 at the bottom of a groove. phi = 0 is +X; +Y (the indicator,
    phi = pi / 2) sits on a crest when the count is a multiple of four."""
    fl = g["flutes"]
    u = (phi * fl["count"] / (2.0 * math.pi)) % 1.0
    v = 0.5 - 0.5 * math.cos(2.0 * math.pi * u)
    return v ** fl.get("sharpness", 1.0)


def build_knob_mesh(g):
    """Vertices (mm) and faces of the whole knob: the revolved, fluted profile closed by a
    flat bottom disc. Returns (verts, faces, info)."""
    profile, info = knob_profile(g)
    m = int(g["angularSamples"])
    count = g["flutes"]["count"]
    if count > 0 and m % count != 0:
        raise ValueError(f"angularSamples ({m}) must be a multiple of the flute count ({count})")
    depth = g["flutes"]["depth"] if count > 0 else 0.0
    phis = [2.0 * math.pi * j / m for j in range(m)]
    shapes = [flute_shape(phi, g) if count > 0 else 0.0 for phi in phis]
    cosines = [math.cos(p) for p in phis]
    sines = [math.sin(p) for p in phis]

    verts = []
    rings = []                                   # vertex index of each ring's first vertex
    for point in profile[:-1]:                   # the last point is the centre
        r, z = point[0], point[1]
        w = flute_weight(point, info, g) * depth
        rings.append(len(verts))
        for j in range(m):
            rr = max(0.0, r - w * shapes[j])
            verts.append((rr * cosines[j], rr * sines[j], z))
    top_centre = len(verts)
    verts.append((0.0, 0.0, profile[-1][1]))
    bottom_centre = len(verts)
    verts.append((0.0, 0.0, 0.0))

    faces = []
    for a, b in zip(rings, rings[1:]):
        for j in range(m):
            k = (j + 1) % m
            faces.append((a + j, a + k, b + k, b + j))
    last = rings[-1]
    for j in range(m):
        faces.append((last + j, last + (j + 1) % m, top_centre))
    first = rings[0]
    for j in range(m):
        faces.append((first + (j + 1) % m, first + j, bottom_centre))
    return verts, faces, info
