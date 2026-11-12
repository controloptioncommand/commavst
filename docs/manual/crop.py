# Нарезает скриншоты теста: python3 crop.py <папка теста> <папка img руководства> <папка картинок README>
import sys
from PIL import Image

src, out, assets = (a.rstrip("/") + "/" for a in sys.argv[1:4])


def save(im, name):
    im.convert("RGB").save(out + name + ".jpg", quality=88)


# координаты — в базовом размере окна 1120×760
TOOLBAR = (14, 14, 1106, 66)
VIEW = (14, 76, 1106, 276)
RACK = (14, 286, 1106, 490)
DETAIL = (14, 490, 1106, 700)
FOOTER = (14, 696, 1106, 746)
MODULES = ["doubler", "harmonizer", "staircase", "autotune", "resonator", "shifter", "varispeed", "spectral"]

shot = {n: Image.open(src + n + ".png") for n in
        ["ui_rubber_main", "ui_oxygen_main", "ui_oxygen_notes", "ui_oxygen_matrix"] + ["ui_oxygen_" + m for m in MODULES]}

save(shot["ui_rubber_main"], "main")
save(shot["ui_oxygen_main"], "main_oxygen")
save(shot["ui_oxygen_main"].crop(TOOLBAR), "toolbar")
save(shot["ui_oxygen_main"].crop(VIEW), "view_pitch")
save(shot["ui_oxygen_notes"].crop(VIEW), "view_notes")
save(shot["ui_oxygen_matrix"].crop(VIEW), "view_matrix")
save(shot["ui_oxygen_main"].crop(RACK), "rack")
save(shot["ui_oxygen_main"].crop(FOOTER), "footer")
for m in MODULES:
    save(shot["ui_oxygen_" + m].crop(DETAIL), "mod_" + m)

# картинки README
for n in ["ui_rubber_main", "ui_oxygen_staircase", "ui_oxygen_matrix", "ui_rubber_notes"]:
    Image.open(src + n + ".png").save(assets + n + ".png", optimize=True)
