#!/bin/sh
# Regenerate include/eyes/*.h (one namespaced header per design; not in git).
cd "$(dirname "$0")/.." || exit 1
mkdir -p include/eyes
for d in catEye defaultEye doeEye dragonEye goatEye naugaEye newtEye noScleraEye; do
    scripts/.venv/bin/python scripts/gen_eye.py "art/$d" 240 "include/eyes/$d.h" "$d" || exit 1
done
