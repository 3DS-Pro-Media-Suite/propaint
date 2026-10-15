"""Draws the UI icons (shapes below) and writes source/data/icons.h: 16x16 alpha masks, drawn at 4x and box-filtered.

Run: python assets/icons/make_icons.py [--preview]   (needs Pillow)
--preview also writes icons_preview.png (every icon at 3x) to the system temp folder.
"""
import math
import os
import sys
import tempfile

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "source", "data", "icons.h")
S = 64  # drawing size, 4x the icon
W = 5  # stroke width at 4x


def arrow_head(d, x, y, dx, dy, size=12):
    # filled triangle pointing along (dx, dy)
    px, py = -dy, dx
    d.polygon([(x + dx * size * 0.2, y + dy * size * 0.2), (x - dx * size + px * size, y - dy * size + py * size),
               (x - dx * size - px * size, y - dy * size - py * size)], fill=255)


def brush(d):
    d.line([(24, 40), (50, 14)], fill=255, width=10)
    d.ellipse((10, 38, 28, 56), fill=255)
    d.polygon([(8, 58), (12, 46), (20, 54)], fill=255)


def eraser(d):
    d.polygon([(30, 10), (54, 34), (36, 52), (12, 28)], outline=255, width=W)
    d.polygon([(21, 19), (45, 43), (36, 52), (12, 28)], fill=255)
    d.line([(34, 56), (56, 56)], fill=255, width=W)


def picker(d):
    d.line([(12, 52), (38, 26)], fill=255, width=6)
    d.ellipse((36, 8, 56, 28), fill=255)
    d.line([(28, 18), (46, 36)], fill=255, width=8)
    d.ellipse((8, 50, 16, 58), fill=255)


def move(d):
    d.line([(32, 10), (32, 54)], fill=255, width=W)
    d.line([(10, 32), (54, 32)], fill=255, width=W)
    for x, y, dx, dy in ((32, 6, 0, -1), (32, 58, 0, 1), (6, 32, -1, 0), (58, 32, 1, 0)):
        arrow_head(d, x, y, dx, dy, 10)


def undo(d):
    d.arc((20, 18, 54, 52), 270, 90, fill=255, width=W)
    d.line([(16, 20), (38, 20)], fill=255, width=W)
    d.line([(24, 50), (38, 50)], fill=255, width=W)
    arrow_head(d, 6, 20, -1, 0, 11)


def layers(d):
    d.polygon([(32, 8), (58, 22), (32, 36), (6, 22)], fill=255)
    d.line([(8, 32), (32, 45), (56, 32)], fill=255, width=W, joint="curve")
    d.line([(8, 43), (32, 56), (56, 43)], fill=255, width=W, joint="curve")


def menu(d):
    for y in (16, 32, 48):
        d.line([(10, y), (54, y)], fill=255, width=6)


def eye(d):
    d.arc((-8, 14, 72, 94), 230, 310, fill=255, width=W)
    d.arc((-8, -30, 72, 50), 50, 130, fill=255, width=W)
    d.ellipse((23, 23, 41, 41), fill=255)


def eyeoff(d):
    eye(d)
    d.line([(8, 6), (56, 58)], fill=0, width=14)
    d.line([(10, 8), (54, 56)], fill=255, width=W)


def lock(d, open_=False):
    d.rounded_rectangle((12, 28, 52, 58), 5, fill=255)
    if open_:
        d.arc((28, 6, 54, 34), 180, 360, fill=255, width=W)
        d.line([(52, 20), (52, 26)], fill=255, width=W)
    else:
        d.arc((19, 8, 45, 36), 180, 360, fill=255, width=W)
        d.line([(21, 20), (21, 30)], fill=255, width=W)
        d.line([(43, 20), (43, 30)], fill=255, width=W)


def plus_px(d):
    d.rectangle((8, 8, 44, 44), outline=255, width=W)
    d.line([(46, 34), (46, 60)], fill=0, width=18)
    d.line([(34, 46), (60, 46)], fill=0, width=18)
    d.line([(46, 36), (46, 58)], fill=255, width=6)
    d.line([(35, 47), (57, 47)], fill=255, width=6)


def plus_sd(d):
    d.ellipse((6, 6, 46, 46), outline=255, width=W)
    d.line([(46, 34), (46, 60)], fill=0, width=18)
    d.line([(34, 46), (60, 46)], fill=0, width=18)
    d.line([(46, 36), (46, 58)], fill=255, width=6)
    d.line([(35, 47), (57, 47)], fill=255, width=6)


def dup(d):
    d.rounded_rectangle((20, 20, 56, 56), 5, outline=255, width=W)
    d.line([(8, 44), (8, 8), (44, 8)], fill=255, width=W, joint="curve")


def up(d):
    d.line([(32, 56), (32, 18)], fill=255, width=6)
    arrow_head(d, 32, 6, 0, -1, 15)


def down(d):
    d.line([(32, 8), (32, 46)], fill=255, width=6)
    arrow_head(d, 32, 58, 0, 1, 15)


def merge(d):
    d.line([(32, 4), (32, 32)], fill=255, width=6)
    arrow_head(d, 32, 42, 0, 1, 13)
    d.rounded_rectangle((8, 48, 56, 58), 3, fill=255)


def trash(d):
    d.line([(8, 16), (56, 16)], fill=255, width=W)
    d.line([(24, 14), (24, 8), (40, 8), (40, 14)], fill=255, width=W)
    d.polygon([(14, 22), (50, 22), (46, 58), (18, 58)], outline=255, width=W)
    d.line([(27, 30), (27, 50)], fill=255, width=4)
    d.line([(37, 30), (37, 50)], fill=255, width=4)


def sliders(d):
    for y, x in ((14, 22), (32, 44), (50, 30)):
        d.line([(6, y), (58, y)], fill=255, width=4)
        d.ellipse((x - 7, y - 7, x + 7, y + 7), fill=255)


def back(d):
    d.line([(40, 10), (18, 32), (40, 54)], fill=255, width=7, joint="curve")


def save(d):
    d.rounded_rectangle((8, 8, 56, 56), 5, outline=255, width=W)
    d.rectangle((20, 8, 42, 24), fill=255)
    d.rectangle((18, 36, 46, 56), outline=255, width=W)


def export(d):
    d.line([(10, 34), (10, 56), (54, 56), (54, 34)], fill=255, width=W, joint="curve")
    d.line([(32, 44), (32, 14)], fill=255, width=6)
    arrow_head(d, 32, 4, 0, -1, 13)


def new(d):
    d.line([(38, 6), (12, 6), (12, 58), (52, 58), (52, 20)], fill=255, width=W, joint="curve")
    d.polygon([(36, 6), (52, 22), (36, 22)], fill=255)


def wifi(d):
    for r in (42, 28):
        d.arc((32 - r, 50 - r, 32 + r, 50 + r), 225, 315, fill=255, width=6)
    d.ellipse((26, 44, 38, 56), fill=255)


def swap(d):
    d.line([(10, 22), (50, 22)], fill=255, width=W)
    arrow_head(d, 58, 22, 1, 0, 11)
    d.line([(14, 44), (54, 44)], fill=255, width=W)
    arrow_head(d, 6, 44, -1, 0, 11)


def sidebar(d):
    d.rounded_rectangle((6, 10, 58, 54), 5, outline=255, width=W)
    d.rectangle((6, 10, 22, 54), fill=255)


def help_(d):
    d.arc((18, 6, 46, 34), 180, 90, fill=255, width=7)
    d.line([(32, 34), (32, 42)], fill=255, width=7)
    d.ellipse((27, 49, 37, 59), fill=255)


def power(d):
    d.arc((8, 10, 56, 58), 300, 240, fill=255, width=6)
    d.line([(32, 4), (32, 30)], fill=255, width=6)


def check(d):
    d.line([(10, 34), (26, 50), (54, 16)], fill=255, width=7, joint="curve")


def clip(d):  # bent arrow: down, then left
    d.line([(44, 6), (44, 40), (20, 40)], fill=255, width=6, joint="curve")
    arrow_head(d, 8, 40, -1, 0, 12)


def folder(d):
    d.polygon([(6, 14), (26, 14), (32, 20), (58, 20), (58, 54), (6, 54)], outline=255, width=W)
    d.line([(6, 28), (58, 28)], fill=255, width=W)


def chev_r(d):
    d.line([(24, 12), (44, 32), (24, 52)], fill=255, width=7, joint="curve")


def chev_d(d):
    d.line([(12, 24), (32, 44), (52, 24)], fill=255, width=7, joint="curve")


def star_pts():
    pts = []
    for k in range(10):
        r = 28 if k % 2 == 0 else 12
        a = -math.pi / 2 + k * math.pi / 5
        pts.append((32 + r * math.cos(a), 34 + r * math.sin(a)))
    return pts


def star(d):
    d.polygon(star_pts(), fill=255)


def star_o(d):
    d.polygon(star_pts(), outline=255, width=W)


def import_(d):
    d.line([(10, 34), (10, 56), (54, 56), (54, 34)], fill=255, width=W, joint="curve")
    d.line([(32, 4), (32, 34)], fill=255, width=6)
    arrow_head(d, 32, 46, 0, 1, 13)


def recolor(d):  # drop
    d.polygon([(32, 4), (50, 34), (14, 34)], fill=255)
    d.ellipse((14, 20, 50, 56), fill=255)


# (enum name, drawing function), in the order of the IC_* enum. REDO is drawn as UNDO mirrored.
ICONS = [
    ("BRUSH", brush), ("ERASER", eraser), ("PICKER", picker), ("MOVE", move), ("UNDO", undo), ("REDO", None),
    ("LAYERS", layers), ("MENU", menu), ("EYE", eye), ("EYE_OFF", eyeoff),
    ("LOCK", lock), ("UNLOCK", lambda d: lock(d, True)),
    ("ADD_PIXEL_LAYER", plus_px), ("ADD_SHAPE_LAYER", plus_sd), ("DUPLICATE", dup), ("UP", up), ("DOWN", down),
    ("MERGE", merge),
    ("TRASH", trash), ("SLIDERS", sliders), ("BACK", back), ("SAVE", save), ("EXPORT", export), ("NEW", new),
    ("WIFI", wifi), ("SWAP", swap), ("SIDEBAR", sidebar), ("HELP", help_), ("POWER", power), ("CHECK", check),
    ("DROP", recolor), ("CLIP", clip), ("FOLDER", folder), ("CHEVRON_RIGHT", chev_r), ("CHEVRON_DOWN", chev_d),
    ("STAR", star), ("STAR_OUTLINE", star_o), ("IMPORT", import_),
]


def draw_all():
    """[(name, 64x64 mask)] in ICONS order."""
    masks = []
    for name, fn in ICONS:
        if name == "REDO":
            im = dict(masks)["UNDO"].transpose(Image.FLIP_LEFT_RIGHT)
        else:
            im = Image.new("L", (S, S), 0)
            fn(ImageDraw.Draw(im))
        masks.append((name, im))
    return masks


def main():
    masks = draw_all()
    out = ["// The UI icons as 16x16 bitmaps. Generated by assets/icons/make_icons.py. Do not edit.\n",
           "#pragma once\n",
           "// clang-format off\n",
           "#define ICON_SIZE 16 // px, square\nenum {\n"]
    out += [f"\tIC_{n},\n" for n, _ in masks]
    out.append("\tIC_COUNT\n};\nstatic const unsigned char ICON_BITMAPS[IC_COUNT][ICON_SIZE * ICON_SIZE] = {\n")
    preview = Image.new("L", (len(masks) * 20, 20), 40)
    for i, (n, im) in enumerate(masks):
        small = im.resize((16, 16), Image.BOX)
        out.append("{" + ",".join(map(str, small.getdata())) + "},\n")
        preview.paste(small, (i * 20 + 2, 2))
    out.append("};\n")
    out.append("// clang-format on\n")
    with open(OUT, "w", newline="\n") as fh:
        fh.write("".join(out))
    print("wrote", os.path.normpath(OUT), "-", len(masks), "icons")
    if "--preview" in sys.argv[1:]:
        path = os.path.join(tempfile.gettempdir(), "icons_preview.png")
        preview.resize((preview.width * 3, preview.height * 3), Image.NEAREST).save(path)
        print("wrote", path)


if __name__ == "__main__":
    main()
