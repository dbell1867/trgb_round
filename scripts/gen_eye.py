#!/usr/bin/env python3
"""Generate an Uncanny Eyes data header at any screen size.

Port of tablegen.py from Adafruit's Uncanny_Eyes (MIT license, Phil Burgess /
Paint Your Dragon for Adafruit Industries). Same encodings; the difference is
that the art (designed for a 128x128 screen) is smoothly upsampled to SCREEN px:

  sclera  RGB565, SCLERA x SCLERA  (art size * scale, Lanczos)
  iris    RGB565 polar texture, unchanged (resolution-independent)
  upper / lower  uint8 eyelid threshold maps, SCREEN x SCREEN (bilinear), from the
          *-symmetrical.png lids (verified identical to Adafruit's dragon header;
          its asymmetrical tables don't match the shipped asymmetrical PNGs)
  polar   uint16 per iris pixel: angle (high 9 bits) | distance-from-edge (low 7)
          computed from the upsampled pupil map, exactly as tablegen.py does

Usage (needs Pillow — use the project venv):
  scripts/.venv/bin/python scripts/gen_eye.py art/dragonEye 240 include/eye_dragon.h
  scripts/.venv/bin/python scripts/gen_eye.py art/catEye 240 include/eyes/catEye.h catEye
The optional 4th argument wraps everything in a C++ namespace (and adds the
design's IRIS_MIN/IRIS_MAX), so several designs can be compiled in together.
All designs at once: scripts/gen_all_eyes.sh
"""
import math
import os
import sys

from PIL import Image

ART_SCREEN = 128  # every Uncanny Eyes design targets a 128x128 screen

# terminatorEye left out: it has no eyelids (maps all 255), so it can't blink or flinch.
# Per-design settings from Adafruit's uncannyEyes/graphics/*.h (IRIS_WIDTH = iris
# diameter in art pixels; IRIS_MIN/MAX = pupil range, sketch default 120/720).
# Designs with a pupilMap.png get their iris size from that image instead.
DESIGNS = {
    "catEye":        dict(iris=128, iris_min=100, iris_max=600),
    "defaultEye":    dict(iris=80,  iris_min=120, iris_max=720),
    "doeEye":        dict(iris=180, iris_min=250, iris_max=500),
    "dragonEye":     dict(iris=160, iris_min=80,  iris_max=400),
    "goatEye":       dict(iris=128, iris_min=120, iris_max=720),
    "naugaEye":      dict(iris=80,  iris_min=300, iris_max=1023),
    "newtEye":       dict(iris=80,  iris_min=180, iris_max=750),
    "noScleraEye":   dict(iris=160, iris_min=120, iris_max=550),
}


def pixels(im):
    """Flat list of pixel values (tuples for RGB) without Pillow's deprecated getdata()."""
    b, n = im.tobytes(), len(im.getbands())
    return list(b) if n == 1 else [tuple(b[i:i + n]) for i in range(0, len(b), n)]


def rgb565(p):
    return ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3)


def even(n):
    n = int(round(n))
    return n + (n & 1)


def emit(out, ctype, name, dims, values, per_line):
    out.write(f"const {ctype} {name}{''.join(f'[{d}]' for d in dims)} = {{\n")
    fmt = "0x{:04X}" if ctype == "uint16_t" else "0x{:02X}"
    for i in range(0, len(values), per_line):
        out.write("  " + ",".join(fmt.format(v) for v in values[i:i + per_line]) + ",\n")
    out.write("};\n\n")


def build(art, screen):
    """Build all tables in memory; returns a dict (also used by preview_lids.py)."""
    scale = screen / ART_SCREEN
    path = lambda f: os.path.join(art, f)

    # Sclera: upsample, keep RGB.
    sc_src = Image.open(path("sclera.png")).convert("RGB")
    sclera_size = even(sc_src.size[0] * scale)
    sc = sc_src.resize((sclera_size, sclera_size), Image.LANCZOS)
    sclera = [rgb565(p) for p in pixels(sc)]

    # Iris polar texture: unchanged.
    ir = Image.open(path("iris.png")).convert("RGB")
    iris = [rgb565(p) for p in pixels(ir)]

    # Eyelid threshold maps (symmetrical versions, else the plain ones): bilinear.
    lids = {}
    for name in ("upper", "lower"):
        f = f"lid-{name}-symmetrical.png"
        if not os.path.exists(path(f)):
            f = f"lid-{name}.png"
        im = Image.open(path(f)).convert("L")
        assert im.size == (ART_SCREEN, ART_SCREEN), f"{f} must be 128x128"
        lids[name] = pixels(im.resize((screen, screen), Image.BILINEAR))

    # Polar map (as tablegen.py, argv 7/8): distance from the upsampled pupil map
    # if there is one, else a round pupil (distance from the centre).
    design = DESIGNS[os.path.basename(os.path.normpath(art))]
    iris_size = even(design["iris"] * scale)
    pm = None
    if os.path.exists(path("pupilMap.png")):
        pm_src = Image.open(path("pupilMap.png")).convert("L")
        assert pm_src.size[0] == design["iris"], "pupilMap size must match the iris size"
        pm = pm_src.resize((iris_size, iris_size), Image.BICUBIC).load()
    radius = iris_size / 2
    polar = []
    for y in range(iris_size):
        dy = y - radius + 0.5
        for x in range(iris_size):
            dx = x - radius + 0.5
            if math.sqrt(dx * dx + dy * dy) >= radius:
                polar.append(127)                       # outside circle
                continue
            angle = (math.atan2(dy, dx) + math.pi) / (math.pi * 2.0)  # 0..<1
            r = pm[x, y] / 255.0 if pm else math.sqrt(dx * dx + dy * dy) / radius
            distance = min(r * 128.0, 127)
            polar.append((int(angle * 512.0) << 7) | (127 - int(distance)))

    return dict(screen=screen, sclera_size=sclera_size, iris_size=iris_size,
                iris_min=design["iris_min"], iris_max=design["iris_max"],
                iris_w=ir.size[0], iris_h=ir.size[1], sclera=sclera, iris=iris,
                upper=lids["upper"], lower=lids["lower"], polar=polar)


def main():
    art, screen, outpath = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    ns = sys.argv[4] if len(sys.argv) > 4 else None
    t = build(art, screen)
    sclera_size, iris_size, ir_w, ir_h = t["sclera_size"], t["iris_size"], t["iris_w"], t["iris_h"]
    sclera, iris, lids, polar = t["sclera"], t["iris"], t, t["polar"]
    with open(outpath, "w") as out:
        out.write(f"// GENERATED by scripts/gen_eye.py from {art} at {screen} px — do not edit.\n")
        out.write("// Art and table format: Adafruit Uncanny_Eyes (MIT license).\n#pragma once\n#include <stdint.h>\n\n")
        if ns:
            out.write(f"namespace {ns} {{\n\n")
            out.write(f"constexpr int IRIS_MIN = {t['iris_min']};\nconstexpr int IRIS_MAX = {t['iris_max']};\n")
        out.write(f"constexpr int SCREEN_SIZE = {screen};\n")
        out.write(f"constexpr int SCLERA_SIZE = {sclera_size};\n")
        out.write(f"constexpr int IRIS_SIZE = {iris_size};\n")
        out.write(f"constexpr int IRIS_MAP_WIDTH = {ir_w};\n")
        out.write(f"constexpr int IRIS_MAP_HEIGHT = {ir_h};\n\n")
        emit(out, "uint16_t", "sclera", (sclera_size, sclera_size), sclera, 12)
        emit(out, "uint16_t", "iris", (ir_h, ir_w), iris, 12)
        emit(out, "uint8_t", "upper", (screen, screen), lids["upper"], 20)
        emit(out, "uint8_t", "lower", (screen, screen), lids["lower"], 20)
        emit(out, "uint16_t", "polar", (iris_size, iris_size), polar, 12)
        if ns:
            out.write(f"}}  // namespace {ns}\n")

    total = 2 * len(sclera) + 2 * len(iris) + len(lids["upper"]) * 2 + 2 * len(polar)
    print(f"{outpath}: screen {screen}, sclera {sclera_size}, iris {iris_size}, "
          f"iris map {ir_w}x{ir_h}, {total / 1024:.0f} KB of tables")


if __name__ == "__main__":
    main()
