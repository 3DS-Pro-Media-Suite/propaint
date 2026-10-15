"""Packs the pre-installed Krita brushes into romfs/brushes and their textures into romfs/textures.

From Krita 4's default resource bundle it takes the presets tagged "My Favorites" and "Ink", except the ones in
SKIPPED and the erasers (any brush erases with the eraser tool), and writes favorites.txt with the "My Favorites"
ones plus EXTRA_FAVORITES. The favourites go on the pop-up palette. On its first start the app imports the
presets into its brush library (source/brush/brush_library.c).

A preset with a texture keeps its PNG inside, base64 encoded twice; that PNG goes to romfs/textures under the
name Krita gives it, with the bundle's EXTRA_TEXTURES beside them (source/brush/textures.c).

Run: python assets/brushes/pack_brushes.py <path to Krita_4_Default_Resources.bundle>
The bundle is in Krita's source tree under krita/data/bundles.
"""
import base64
import os
import re
import struct
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROMFS = os.path.join(HERE, "..", "..", "romfs")
BRUSHES_OUT = os.path.join(ROMFS, "brushes")
TEXTURES_OUT = os.path.join(ROMFS, "textures")
FAVORITES_TAG = "★ My Favorites"
WANTED_TAGS = (FAVORITES_TAG, "Ink")
ERASERS_TAG = "Erasers"
SKIPPED = {
    "d)_Ink-8_Sumi-e.kpp",
    "v)_Distort_Move.kpp",
    "v)_Sketching-1_Chrome_Thin.kpp",
    "v)_Sketching-2_Chrome_Large.kpp",
    "v)_Sketching-3_Leaky.kpp",
    "y)_Screentone_Moire.kpp",
    "y)_Texture_Starfield.kpp",
    "z)_Stamp_Hearts.kpp",
    "z)_Stamp_Shoujo_Bubbles.kpp",
}
EXTRA_FAVORITES = ("d)_Ink-3_Gpen.kpp",)  # takes the palette slot of the skipped Distort Move
EXTRA_TEXTURES = ("01_canvas.png", "04_paper-C-grain.png", "06_hard-grain.png", "09_drawed_crosshatched.png")
ENTRY = re.compile(r'<manifest:file-entry[^>]*manifest:full-path="(paintoppresets/[^"]+\.kpp)"[^>]*>'
                   r'(.*?)</manifest:file-entry>', re.S)
TAG = re.compile(r"<manifest:tag>([^<]*)</manifest:tag>")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PRESET_KEYWORD = b"preset"


def param(xml, name):
    """The value of <param name="NAME">, without a CDATA wrapper, or None."""
    match = re.search(r'name="%s"[^>]*>(?:<!\[CDATA\[)?(.*?)(?:\]\]>)?</param>' % re.escape(name), xml, re.S)
    return match.group(1) if match else None


def preset_xml(kpp):
    """The preset XML in a .kpp file's "preset" text chunk."""
    position = len(PNG_SIGNATURE)
    while position < len(kpp):
        length, kind = struct.unpack(">I4s", kpp[position:position + 8])
        body = kpp[position + 8:position + 8 + length]
        position += 12 + length
        keyword, _, text = body.partition(b"\0")
        if keyword != PRESET_KEYWORD:
            continue
        if kind == b"zTXt":
            return zlib.decompress(text[1:]).decode("utf-8")
        if kind == b"tEXt":
            return text.decode("utf-8")
    return ""


def texture_of(kpp):
    """(name, PNG bytes) of the preset's texture, or None."""
    xml = preset_xml(kpp)
    if param(xml, "Texture/Pattern/Enabled") != "true":
        return None
    png = base64.b64decode(param(xml, "Texture/Pattern/Pattern"))
    if not png.startswith(PNG_SIGNATURE):
        png = base64.b64decode(png)
    return param(xml, "Texture/Pattern/Name"), png


def empty_folder(folder):
    os.makedirs(folder, exist_ok=True)
    for name in os.listdir(folder):
        os.remove(os.path.join(folder, name))


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    bundle = zipfile.ZipFile(sys.argv[1])
    manifest = bundle.read("META-INF/manifest.xml").decode("utf-8")
    picked, favorites = [], []
    for path, body in ENTRY.findall(manifest):
        tags = TAG.findall(body)
        name = os.path.basename(path)
        if name in SKIPPED or ERASERS_TAG in tags or not any(tag in WANTED_TAGS for tag in tags):
            continue
        picked.append(path)
        if FAVORITES_TAG in tags:
            favorites.append(name)
    favorites.extend(EXTRA_FAVORITES)
    empty_folder(BRUSHES_OUT)
    empty_folder(TEXTURES_OUT)
    textures = {name: bundle.read("patterns/" + name) for name in EXTRA_TEXTURES}
    total = 0
    for path in sorted(picked):
        data = bundle.read(path)
        total += len(data)
        with open(os.path.join(BRUSHES_OUT, os.path.basename(path)), "wb") as fh:
            fh.write(data)
        texture = texture_of(data)
        if texture:
            textures[texture[0]] = texture[1]
    with open(os.path.join(BRUSHES_OUT, "favorites.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(favorites) + "\n")
    for name, png in textures.items():
        with open(os.path.join(TEXTURES_OUT, name), "wb") as fh:
            fh.write(png)
    texture_bytes = sum(len(png) for png in textures.values())
    print(f"{len(picked)} presets ({total // 1024} KB), {len(favorites)} favourites -> romfs/brushes")
    print(f"{len(textures)} textures ({texture_bytes // 1024} KB) -> romfs/textures")


if __name__ == "__main__":
    main()
