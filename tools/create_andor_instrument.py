#!/usr/bin/env python3
"""Create a new ANDOR instrument from the ANDOR Instrument Shell.

    python3 tools/create_andor_instrument.py --name "ANDOR Example" --slug example \\
        --plugin-code Aexm --bundle-id com.osp.andorexample

Writes products/andor-<slug>/ from shared/andor-shell/template (CMakeLists.txt, Source/
Product.cpp, README.md, product.json), adds the product to products/CMakeLists.txt and to
andor-design-system/product-registry.json. It never overwrites anything: an existing folder,
or a name, slug, plugin code, bundle ID, state type or preset extension any ANDOR plugin
already uses (ANDOR OSP, Drums, Grain, Phrase, every product in the registry), stops it
before a single file is written. Placeholders are filled in the new files only; nothing
else in the repository is searched or replaced.

The new product looks exactly like ANDOR OSP (the shared interface) and plays the shell's
labelled diagnostic engine until its own engine is written: see andor-design-system/HANDOFF.md.
"""

import argparse
import datetime
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEMPLATE = os.path.join(ROOT, "shared", "andor-shell", "template")
PRODUCTS = os.path.join(ROOT, "products")
REGISTRY = os.path.join(ROOT, "andor-design-system", "product-registry.json")

# The header shows "ANDOR" then "/<IDENTITY>" in the 60 px display face: longer words reach
# the preset bar. The shell's test measures the real glyphs; this is the quick first check.
MAX_IDENTITY = 8
MAX_TAGLINE = 32


class Refused(Exception):
    pass


def validate(args):
    if not re.fullmatch(r"ANDOR [A-Z][A-Za-z0-9 ]{1,30}", args.name):
        raise Refused("--name must read 'ANDOR <Name>' (letters, digits and spaces, up to 30 after ANDOR)")
    if not re.fullmatch(r"[a-z][a-z0-9]{1,23}", args.slug):
        raise Refused("--slug must be 2-24 lower-case letters and digits, starting with a letter")
    if not (len(args.plugin_code) == 4 and re.fullmatch(r"[A-Za-z0-9]{4}", args.plugin_code)
            and re.search(r"[A-Z]", args.plugin_code)):
        # Apple reserves all-lower-case codes for itself.
        raise Refused("--plugin-code must be 4 letters or digits with at least one capital (e.g. Aexm)")
    if not re.fullmatch(r"[a-z][a-z0-9]*(\.[a-z][a-z0-9-]*){2,}", args.bundle_id):
        raise Refused("--bundle-id must look like com.osp.andorexample (lower case, at least three parts)")
    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        raise Refused("--version must be MAJOR.MINOR.PATCH")
    if not re.fullmatch(r"[A-Z0-9]{1,%d}" % MAX_IDENTITY, args.identity):
        raise Refused("--identity must be 1-%d capitals or digits (it follows 'ANDOR/' in the header)" % MAX_IDENTITY)
    if not re.fullmatch(r"[A-Za-z0-9 ,.'-]{1,%d}" % MAX_TAGLINE, args.tagline):
        raise Refused("--tagline must be up to %d plain characters" % MAX_TAGLINE)


def derived(args):
    words = re.sub(r"[^A-Za-z0-9]", "", args.name.title())
    return {
        "PRODUCT_NAME": args.name,
        "SLUG": args.slug,
        "PLUGIN_CODE": args.plugin_code,
        "BUNDLE_ID": args.bundle_id,
        "VERSION": args.version,
        "IDENTITY": args.identity,
        "TAGLINE": args.tagline,
        "TARGET": words.replace("Andor", "ANDOR_", 1) if words.startswith("Andor") else "ANDOR_" + words,
        "TEST_NAME": "andor_%s_tests" % args.slug,
        "PRESET_EXTENSION": ".andor%s" % args.slug,
        "STATE_TYPE": "ANDOR_%s" % args.slug.upper(),
        "DATE": datetime.date.today().isoformat(),
    }


def check_unique(values, registry, folder):
    if os.path.exists(folder):
        raise Refused("%s already exists: nothing was written (remove it yourself if it should go)" % os.path.relpath(folder, ROOT))
    for entry in registry.get("products", []):
        clashes = []
        if entry.get("slug") == values["SLUG"]:
            clashes.append("slug %s" % values["SLUG"])
        for key, label in (("name", "name"), ("productName", "name")):
            if entry.get(key, "").lower() == values["PRODUCT_NAME"].lower():
                clashes.append("%s %s" % (label, values["PRODUCT_NAME"]))
        if entry.get("pluginCode") == values["PLUGIN_CODE"]:
            clashes.append("plugin code %s" % values["PLUGIN_CODE"])
        if entry.get("bundleId", "").lower() == values["BUNDLE_ID"].lower():
            clashes.append("bundle ID %s" % values["BUNDLE_ID"])
        if entry.get("stateType") == values["STATE_TYPE"]:
            clashes.append("state type %s" % values["STATE_TYPE"])
        if entry.get("presetExtension") == values["PRESET_EXTENSION"]:
            clashes.append("preset extension %s" % values["PRESET_EXTENSION"])
        if entry.get("target") == values["TARGET"]:
            clashes.append("CMake target %s" % values["TARGET"])
        if clashes:
            raise Refused("%s already uses the %s (product-registry.json): nothing was written"
                          % (entry.get("name", "?"), ", ".join(sorted(set(clashes)))))


def fill(text, values):
    def one(match):
        key = match.group(1)
        if key not in values:
            raise Refused("template placeholder {{%s}} has no value" % key)
        return values[key]
    return re.sub(r"\{\{([A-Z_]+)\}\}", one, text)


def plan(values, folder):
    """Every file the product needs, as (path, text), read and filled before anything is written."""
    files = []
    for directory, _, names in os.walk(TEMPLATE):
        for name in sorted(names):
            if not name.endswith(".in"):
                continue
            source = os.path.join(directory, name)
            relative = os.path.relpath(source, TEMPLATE)[:-3]
            with open(source, encoding="utf-8") as f:
                files.append((os.path.join(folder, relative), fill(f.read(), values)))
    return files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--name", required=True, help="product name, e.g. 'ANDOR Example'")
    parser.add_argument("--slug", required=True, help="lower-case namespace, e.g. example")
    parser.add_argument("--plugin-code", required=True, help="4 characters, at least one capital, e.g. Aexm")
    parser.add_argument("--bundle-id", required=True, help="e.g. com.osp.andorexample")
    parser.add_argument("--version", default="0.1.0")
    parser.add_argument("--identity", help="the word after 'ANDOR/' in the header (default: the name's last word, in capitals)")
    parser.add_argument("--tagline", default="Built from the ANDOR shell", help="the line under the identity")
    parser.add_argument("--dry-run", action="store_true", help="check and show what would be written; write nothing")
    args = parser.parse_args(argv)
    if args.identity is None:
        args.identity = re.sub(r"[^A-Za-z0-9]", "", args.name.split()[-1]).upper()

    try:
        validate(args)
        values = derived(args)
        with open(REGISTRY, encoding="utf-8") as f:
            registry = json.load(f)
        folder = os.path.join(PRODUCTS, "andor-" + args.slug)
        check_unique(values, registry, folder)
        files = plan(values, folder)
        products_cmake = os.path.join(PRODUCTS, "CMakeLists.txt")
        line = "add_subdirectory(andor-%s)\n" % args.slug
        if os.path.exists(products_cmake):
            with open(products_cmake, encoding="utf-8") as f:
                existing = f.read()
            if line.strip() in existing:
                raise Refused("products/CMakeLists.txt already adds andor-%s: nothing was written" % args.slug)
    except Refused as refused:
        print("create_andor_instrument: " + str(refused), file=sys.stderr)
        return 2

    print("%s (%s, plugin code %s, bundle %s)" % (values["PRODUCT_NAME"], values["SLUG"], values["PLUGIN_CODE"], values["BUNDLE_ID"]))
    for path, _ in files:
        print("  write  " + os.path.relpath(path, ROOT))
    print("  append products/CMakeLists.txt: " + line.strip())
    print("  append andor-design-system/product-registry.json: " + values["SLUG"])
    if args.dry_run:
        print("dry run: nothing written")
        return 0

    # Write: the new folder first (exclusive creation: never over an existing file), then
    # the two appends.
    for path, text in files:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "x", encoding="utf-8") as f:
            f.write(text)
    if not os.path.exists(products_cmake):
        with open(products_cmake, "x", encoding="utf-8") as f:
            f.write("# ANDOR instruments built from the ANDOR Instrument Shell (tools/create_andor_instrument.py adds them).\n")
    with open(products_cmake, "a", encoding="utf-8") as f:
        f.write(line)
    registry.setdefault("products", []).append({
        "name": values["PRODUCT_NAME"],
        "productName": values["PRODUCT_NAME"],
        "slug": values["SLUG"],
        "pluginCode": values["PLUGIN_CODE"],
        "bundleId": values["BUNDLE_ID"],
        "stateType": values["STATE_TYPE"],
        "presetExtension": values["PRESET_EXTENSION"],
        "target": values["TARGET"],
        "location": "products/andor-" + values["SLUG"],
        "builtFromShell": True,
    })
    with open(REGISTRY, "w", encoding="utf-8") as f:
        json.dump(registry, f, indent=2)
        f.write("\n")
    print("done. Next: build %s_All and run ctest -R %s (see %s/README.md)"
          % (values["TARGET"], values["TEST_NAME"], os.path.relpath(folder, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
