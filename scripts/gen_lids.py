#!/usr/bin/env python3
"""Generate a pumpkin-flesh eyelid texture + PC preview of the finished eye.

The texture is indexed [depth][x], where depth = how far a covered pixel is
inside the lid, in lid-map threshold units:  depth = threshold - map  (>= 0).
Because depth is measured from the lid's moving edge, the texture slides with
the lid instead of being a fixed picture the lid wipes across.

  depth 0..EDGE        cut edge: glowing yellow flesh with fibres (lit from inside)
  depth ~EDGE..+2      thin dark rind line
  deeper               orange skin with vertical ribs and fine grain

Writes include/lid_pumpkin.h and a preview PNG that renders the eye with the same
maths as src/main.cpp (drawEye), so the look can be checked before flashing.

Usage (project venv: Pillow + numpy):
  scripts/.venv/bin/python scripts/gen_lids.py art/dragonEye include/lid_pumpkin.h docs/img/pumpkin-lids.png
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import gen_eye  # noqa: E402

SCREEN = 240
DEPTHS = 256
EDGE = 16                     # width of the glowing cut-flesh band, threshold units
SHADOW = 10                   # width of the shadow the lid casts on the eye
SHADOW_MAX = 20               # darkest shadow, out of 32 (matches main.cpp)
RIB_GAP = (65, 110)           # px between rib grooves, picked at random (~2-3 ribs across)
SEED = 31                     # Halloween

FLESH_GLOW = np.array([1.00, 0.88, 0.42])   # cut edge, brightest (touching the light)
FLESH_DEEP = np.array([0.98, 0.58, 0.10])   # cut edge, far side
RIND = np.array([0.45, 0.17, 0.02])
SKIN_HI = np.array([1.00, 0.56, 0.10])      # middle of a rib
SKIN = np.array([0.88, 0.40, 0.04])
SKIN_GROOVE = np.array([0.42, 0.14, 0.01])  # bottom of a groove


def smooth_noise(rng, shape, blur_d, blur_x):
    """Random field blurred more along depth than x -> streaks along the depth axis."""
    n = rng.standard_normal(shape)
    for axis, r in ((0, blur_d), (1, blur_x)):
        if r > 0:
            k = np.exp(-0.5 * (np.arange(-int(3 * r), int(3 * r) + 1) / r) ** 2)
            k /= k.sum()
            n = np.apply_along_axis(lambda v: np.convolve(np.pad(v, int(3 * r), mode="wrap"), k, "valid"), axis, n)
    return n / n.std()


def texture():
    rng = np.random.default_rng(SEED)
    d = np.arange(DEPTHS)[:, None].astype(float)
    x = np.arange(SCREEN)[None, :].astype(float)

    # Skin: each rib is a rounded bulge between two grooves. Grooves are spaced
    # irregularly (RIB_GAP); u = 0..1 across whichever rib x falls in. A gentle,
    # slow sway with depth keeps the grooves from looking ruled.
    grooves = [-int(rng.integers(0, 60))]
    while grooves[-1] < SCREEN + RIB_GAP[1]:
        grooves.append(grooves[-1] + int(rng.integers(RIB_GAP[0], RIB_GAP[1] + 1)))
    grooves = np.array(grooves, float)
    xs = x + 3 * np.sin(d / 60.0 + x / 90.0)
    i = np.clip(np.searchsorted(grooves, xs, side="right") - 1, 0, len(grooves) - 2)
    u = (xs - grooves[i]) / (grooves[i + 1] - grooves[i])
    bulge = np.sin(np.pi * u)                       # 0 at the grooves, 1 mid-rib
    light = np.clip(bulge * (0.75 + 0.5 * (0.5 - u)), 0, 1)   # lit from the left
    t = light[..., None]
    skin = np.where(t < 0.35, SKIN_GROOVE + (SKIN - SKIN_GROOVE) * np.clip(t / 0.35, 0, 1) ** 0.6,
                    SKIN + (SKIN_HI - SKIN) * np.clip((t - 0.35) / 0.65, 0, 1) ** 1.5)
    grain = smooth_noise(rng, (DEPTHS, SCREEN), 8, 2)[..., None]
    skin = skin * (1 + 0.035 * grain)

    # Cut edge: glow fading into deeper flesh, with stronger fibres.
    f = np.clip(d / EDGE, 0, 1)[..., None]
    flesh = FLESH_GLOW + (FLESH_DEEP - FLESH_GLOW) * f ** 0.8
    fibres = smooth_noise(rng, (DEPTHS, SCREEN), 3, 1.5)[..., None]
    flesh = flesh * (1 + 0.05 * fibres)

    # Blend flesh -> rind -> skin across the band boundary.
    rind_w = np.exp(-0.5 * ((d - EDGE - 1) / 1.0) ** 2)[..., None]
    to_skin = np.clip((d - EDGE) / 3.0, 0, 1)[..., None]
    tex = flesh * (1 - to_skin) + skin * to_skin
    tex = tex * (1 - 0.8 * rind_w) + RIND * 0.8 * rind_w
    return np.clip(tex, 0, 1)


def to565(rgb):
    c = (np.clip(rgb, 0, 1) * 255).astype(np.uint32)
    return ((c[..., 0] & 0xF8) << 8) | ((c[..., 1] & 0xFC) << 3) | (c[..., 2] >> 3)


def from565(c):
    c = c.astype(np.uint32)
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.uint8)


def render(t, tex565, iscale, sx, sy, uT, lT):
    """numpy mirror of drawEye() in src/main.cpp. Returns 240x240 RGB565."""
    S, SC, IS = SCREEN, t["sclera_size"], t["iris_size"]
    sclera = np.array(t["sclera"], np.uint32).reshape(SC, SC)
    iris = np.array(t["iris"], np.uint32).reshape(t["iris_h"], t["iris_w"])
    polar = np.array(t["polar"], np.uint32).reshape(IS, IS)
    up = np.array(t["upper"], np.int32).reshape(S, S)
    lo = np.array(t["lower"], np.int32).reshape(S, S)

    thr = (128 * (1023 - iscale) + 512) // 1024
    iscale_fx = t["iris_h"] * 65536 // thr
    off = (SC - IS) // 2
    yy, xx = np.mgrid[0:S, 0:S]
    scY, scX = sy + yy, sx + xx
    irY, irX = scY - off, scX - off
    inbox = (irY >= 0) & (irY < IS) & (irX >= 0) & (irX < IS)
    pol = polar[np.clip(irY, 0, IS - 1), np.clip(irX, 0, IS - 1)]
    dist = pol & 0x7F
    in_iris = inbox & (dist < thr)
    row = np.minimum(dist * iscale_fx // 65536, t["iris_h"] - 1)
    col = (t["iris_w"] * (pol >> 7)) // 512
    p = np.where(in_iris, iris[row, col], sclera[scY, scX])

    # Shadow from the nearer lid edge on uncovered pixels.
    gap = np.minimum(up - uT, lo - lT)                       # > 0: uncovered
    shade = np.where((gap > 0) & (gap <= SHADOW), SHADOW_MAX * (SHADOW + 1 - gap) // SHADOW, 0)
    rgb = from565(p).astype(np.int32)
    rgb = rgb - rgb * shade[..., None] // 32

    # Lids: texture by depth from the nearest covering edge.
    du = np.where(up <= uT, uT - up, 999)
    dl = np.where(lo <= lT, lT - lo, 999)
    depth = np.minimum(du, dl)
    covered = depth < 999
    lid = from565(tex565[np.clip(depth, 0, DEPTHS - 1), xx]).astype(np.int32)
    rgb = np.where(covered[..., None], lid, rgb)
    return rgb.astype(np.uint8)


def thresholds(t, sy, closure):
    S = SCREEN
    up = np.array(t["upper"]).reshape(S, S)
    sx = 30
    sxp, syp = t["sclera_size"] // 2 - sx // 2, t["sclera_size"] // 2 - (sy + t["iris_size"] // 4)
    u = (int(up[syp, sxp]) + int(up[syp, S - 1 - sxp])) // 2 if syp >= 0 else 0
    return sx, int(u + (254 - u) * closure), int((254 - u) + u * closure)


def main():
    art, header, preview = sys.argv[1], sys.argv[2], sys.argv[3]
    t = gen_eye.build(art, SCREEN)
    tex565 = to565(texture())

    with open(header, "w") as out:
        out.write("// GENERATED by scripts/gen_lids.py — do not edit.\n"
                  "// Pumpkin-flesh eyelid texture, indexed [depth into lid][x].\n#pragma once\n#include <stdint.h>\n\n")
        out.write(f"constexpr int LID_DEPTHS = {DEPTHS};\nconstexpr int LID_EDGE = {EDGE};\n"
                  f"constexpr int LID_SHADOW = {SHADOW};\nconstexpr int LID_SHADOW_MAX = {SHADOW_MAX};\n\n")
        gen_eye.emit(out, "uint16_t", "lidTex", (DEPTHS, SCREEN), tex565.flatten().tolist(), 12)

    states = [("looking up", 55, 0.0), ("centre", 30, 0.0), ("looking down", 5, 0.0),
              ("blink 50%", 30, 0.5), ("blink 85%", 30, 0.85)]
    tiles = []
    for name, sy, c in states:
        sx, uT, lT = thresholds(t, sy, c)
        img = Image.fromarray(render(t, tex565, 240, sx, sy, uT, lT)).resize((480, 480), Image.NEAREST)
        mask = Image.new("L", (480, 480), 0)
        from PIL import ImageDraw
        ImageDraw.Draw(mask).ellipse((0, 0, 479, 479), fill=255)
        tile = Image.new("RGB", (480, 480), (40, 40, 40))
        tile.paste(img, (0, 0), mask)
        tiles.append(tile)
        print(f"{name:12s} sy={sy:2d} uT={uT:3d} lT={lT:3d}")
    sheet = Image.new("RGB", (480 * len(tiles) + 20 * (len(tiles) - 1), 480), (40, 40, 40))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, (i * 500, 0))
    os.makedirs(os.path.dirname(preview), exist_ok=True)
    sheet.save(preview)
    print(f"{header}: {DEPTHS}x{SCREEN} texture, {tex565.size * 2 // 1024} KB;  preview {preview}")


if __name__ == "__main__":
    main()
