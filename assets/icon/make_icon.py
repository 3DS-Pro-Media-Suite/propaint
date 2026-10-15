"""Draws icon.png in the repo root, the 48x48 HOME Menu icon: a paintbrush and its stroke, cream on a dark tile.

Same style as Pro Cam's and Pro Rec's icons. Shapes are built with shapely, drawn at 16x and filtered down to 48 px.
Run: python assets/icon/make_icon.py [preview.png]   (needs Pillow and shapely; the preview is the icon at 8x)
"""
import math
import os
import sys

from PIL import Image, ImageDraw
from shapely import affinity
from shapely.geometry import LineString, MultiPolygon, Polygon, box
from shapely.ops import unary_union

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "icon.png")

N = 48  # icon size
S = 16  # supersampling
W = N * S

BG = (12, 12, 14)
INK = (238, 235, 228)


def rrect(x0, y0, x1, y1, r):
    return box(x0 + r, y0 + r, x1 - r, y1 - r).buffer(r, quad_segs=48)


def brush():
    """Built upright around x = 24 with the tip at the bottom; glyph() tilts it."""
    handle = rrect(21.6, 2.0, 26.4, 23.5, 2.4)
    ferrule = box(20.6, 24.6, 27.4, 29.6)
    tip = Polygon([(20.6, 30.6), (27.4, 30.6), (27.4, 33.5), (24.0, 41.0), (20.6, 33.5)]).buffer(0.6, quad_segs=24)
    tip = tip.intersection(box(0, 30.6, 48, 48))
    return unary_union([handle, ferrule, tip])


def stroke():
    """One sine wave along the bottom, x 9 to 40."""
    xs = [9.0 + i * 0.5 for i in range(63)]
    pts = [(x, 40.0 + 2.4 * math.sin((x - 9.0) / 31.0 * 2 * math.pi)) for x in xs]
    return LineString(pts).buffer(1.6, cap_style=1, quad_segs=24)


def glyph():
    b = affinity.rotate(brush(), 38, origin=(24, 24))
    b = affinity.translate(b, 1.0, -3.5)
    return unary_union([b, stroke()])


def raster(g):
    m = Image.new("L", (W, W), 0)
    d = ImageDraw.Draw(m)
    for p in (g.geoms if isinstance(g, MultiPolygon) else [g]):
        d.polygon([(x * S, y * S) for x, y in p.exterior.coords], fill=255)
        for h in p.interiors:
            d.polygon([(x * S, y * S) for x, y in h.coords], fill=0)
    return m


def main():
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    tile = Image.new("L", (W, W), 0)
    ImageDraw.Draw(tile).rounded_rectangle([0, 0, W, W], radius=8 * S, fill=255)
    img.paste(Image.new("RGBA", (W, W), BG + (255,)), (0, 0), tile)
    img.paste(Image.new("RGBA", (W, W), INK + (255,)), (0, 0), raster(glyph()))

    icon = img.resize((N, N), Image.LANCZOS)
    icon.save(OUT)
    print("wrote", os.path.normpath(OUT))
    if len(sys.argv) > 1:
        img.resize((N * 8, N * 8), Image.LANCZOS).save(sys.argv[1])
        print("wrote", sys.argv[1])


if __name__ == "__main__":
    main()
