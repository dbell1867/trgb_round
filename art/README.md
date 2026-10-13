# Eye art

Each folder is copied unchanged from Adafruit's
[Uncanny_Eyes](https://github.com/adafruit/Uncanny_Eyes) (`convert/<name>/`, commit `d2103e8`),
MIT license, © Adafruit Industries (Phil Burgess / Paint Your Dragon):
catEye, defaultEye, doeEye, dragonEye, goatEye, naugaEye, newtEye, noScleraEye.
(terminatorEye is left out — it has no eyelids, so it can't blink or flinch.)

`scripts/gen_all_eyes.sh` turns them into `include/eyes/<name>.h` (not in git);
`scripts/preview_eyes.py` renders the contact sheet `docs/img/eye-designs.png`.
