#!/usr/bin/env python3
"""Visual QA: compare the live editor against the approved reference images.

For every design/reference/<name>.png that has a design/current/<name>.png (rendered by
scripts/visual-review.sh), writes design/review/<name>/:
  reference.png       the approved image
  implementation.png  the live editor at the same size
  overlay-50.png      50 % reference / 50 % implementation (edges that jump are wrong)
  diff.png            per-pixel difference, amplified (dark = equal)
  blink.gif           alternates the two (1 s each) for manual review
  side-by-side.png    the two next to each other
and prints the mean difference per image. Optional crop for comparison boards:
  visual-compare.py --crop x,y,w,h --name knob-macro  (reference coordinates)
Needs Pillow (pip install pillow).
"""
import argparse
import pathlib
import sys

from PIL import Image, ImageChops, ImageOps

root = pathlib.Path(__file__).resolve().parent.parent
reference_dir = root / "design" / "reference"
current_dir = root / "design" / "current"
review_dir = root / "design" / "review"


def compare(name, ref, cur, out):
    out.mkdir(parents=True, exist_ok=True)
    if cur.size != ref.size:
        cur = cur.resize(ref.size, Image.LANCZOS)
    ref.save(out / "reference.png")
    cur.save(out / "implementation.png")
    Image.blend(ref, cur, 0.5).save(out / "overlay-50.png")
    diff = ImageChops.difference(ref, cur)
    ImageOps.autocontrast(diff.convert("L"), cutoff=1).save(out / "diff.png")
    ref.save(out / "blink.gif", save_all=True, append_images=[cur], duration=1000, loop=0)
    board = Image.new("RGB", (ref.width * 2 + 12, ref.height), (255, 255, 255))
    board.paste(ref, (0, 0))
    board.paste(cur, (ref.width + 12, 0))
    board.save(out / "side-by-side.png")
    histogram = diff.convert("L").histogram()
    mean = sum(level * count for level, count in enumerate(histogram)) / (ref.width * ref.height)
    print(f"{name:28s} mean difference {mean:6.2f} / 255")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--crop", help="x,y,w,h in reference pixels")
    parser.add_argument("--name", help="reference name (without .png) for --crop, or a label")
    parser.add_argument("--reference", help="reference image for --crop (default design/reference/main-2-layer.png)")
    parser.add_argument("--current", help="implementation image for --crop (default design/current/main-2-layer.png)")
    args = parser.parse_args()

    if args.crop:
        x, y, w, h = (int(v) for v in args.crop.split(","))
        ref = Image.open(args.reference or reference_dir / "main-2-layer.png").convert("RGB")
        cur = Image.open(args.current or current_dir / "main-2-layer.png").convert("RGB")
        if cur.size != ref.size:
            cur = cur.resize(ref.size, Image.LANCZOS)
        box = (x, y, x + w, y + h)
        compare(args.name or f"crop-{x}-{y}", ref.crop(box), cur.crop(box), review_dir / "crops" / (args.name or f"crop-{x}-{y}"))
        return 0

    found = 0
    for ref_path in sorted(reference_dir.glob("*.png")):
        cur_path = current_dir / ref_path.name
        if not cur_path.exists():
            print(f"{ref_path.name:28s} (no implementation render)")
            continue
        found += 1
        compare(ref_path.stem, Image.open(ref_path).convert("RGB"), Image.open(cur_path).convert("RGB"), review_dir / ref_path.stem)
    if found == 0:
        print("No reference with a matching render. Put the approved PNGs in design/reference/ (see design/README.md).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
