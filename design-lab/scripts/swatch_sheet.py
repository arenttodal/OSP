"""Swatch sheet: each material tile, plus a 4x crop of its centre (grain at pixel level)."""
import glob, os, sys
from PIL import Image

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GROUND = (232, 225, 214)


def main(out):
    files = sorted(glob.glob(os.path.join(LAB, "renders", "materials", "M*.png")))
    tiles = [Image.open(f).convert("RGBA") for f in files]
    w, h = tiles[0].size
    sheet = Image.new("RGB", (w * len(tiles), h + w), GROUND)
    for i, t in enumerate(tiles):
        bg = Image.new("RGBA", t.size, GROUND + (255,))
        sheet.paste(Image.alpha_composite(bg, t).convert("RGB"), (i * w, 0))
        c = w // 8
        crop = t.crop((w // 2 - c, h // 2 - c, w // 2 + c, h // 2 + c)).resize((w, w), Image.NEAREST)
        sheet.paste(Image.alpha_composite(Image.new("RGBA", crop.size, GROUND + (255,)), crop).convert("RGB"), (i * w, h))
    sheet.save(out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(LAB, "work", "swatches-preview.png"))
