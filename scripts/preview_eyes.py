#!/usr/bin/env python3
"""Contact sheet of every eye design in art/, rendered with the pumpkin lids
exactly as the firmware would (gen_lids.render mirrors drawEye in main.cpp).

Usage (project venv):
  scripts/.venv/bin/python scripts/preview_eyes.py art docs/img/eye-designs.png
"""
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
import gen_eye   # noqa: E402
import gen_lids  # noqa: E402

TILE = 360


def main():
    art_root, out = sys.argv[1], sys.argv[2]
    tex565 = gen_lids.to565(gen_lids.texture())
    names = sorted(n for n in gen_eye.DESIGNS if os.path.isdir(os.path.join(art_root, n)))
    cols = 3
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * TILE, rows * (TILE + 30)), (40, 40, 40))
    mask = Image.new("L", (TILE, TILE), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, TILE - 1, TILE - 1), fill=255)
    for i, name in enumerate(names):
        t = gen_eye.build(os.path.join(art_root, name), gen_lids.SCREEN)
        c = (t["sclera_size"] - gen_lids.SCREEN) // 2            # looking straight ahead
        iscale = (t["iris_min"] + t["iris_max"]) // 2
        # Lid tracking as in Lids::update: sample the upper map above the pupil.
        up = t["upper"]
        sx_p = t["sclera_size"] // 2 - c // 2
        sy_p = t["sclera_size"] // 2 - (c + t["iris_size"] // 4)
        S = gen_lids.SCREEN
        u = (up[sy_p * S + sx_p] + up[sy_p * S + S - 1 - sx_p]) // 2 if 0 <= sy_p < S and sx_p < S else 0
        img = gen_lids.render(t, tex565, iscale, c, c, u, 254 - u)
        tile = Image.fromarray(img).resize((TILE, TILE), Image.NEAREST)
        x, y = (i % cols) * TILE, (i // cols) * (TILE + 30)
        sheet.paste(tile, (x, y), mask)
        ImageDraw.Draw(sheet).text((x + 10, y + TILE + 8),
                                   f"{name}  sclera {t['sclera_size']} iris {t['iris_size']} "
                                   f"travel {t['sclera_size'] - S}", fill=(230, 230, 230))
        print(f"{name:14s} sclera {t['sclera_size']:3d}  iris {t['iris_size']:3d}  "
              f"iris map {t['iris_w']}x{t['iris_h']}  pupil {t['iris_min']}-{t['iris_max']}  u {u}")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main()
