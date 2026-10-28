#!/usr/bin/env python3
"""Tests of tools/create_andor_instrument.py, in a sandbox (a temporary copy of the
template, products/ and the registry): never the repository's own files.

    python3 tools/tests/test_create_andor_instrument.py
"""

import importlib.util
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


def load_tool():
    spec = importlib.util.spec_from_file_location("create_andor_instrument", os.path.join(REPO, "tools", "create_andor_instrument.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class Sandbox:
    """A fake repository: the real template, an empty products/ and a copy of the registry."""

    def __init__(self):
        self.root = tempfile.mkdtemp(prefix="andor-bootstrap-test-")
        shutil.copytree(os.path.join(REPO, "shared", "andor-shell", "template"), os.path.join(self.root, "shared", "andor-shell", "template"))
        os.makedirs(os.path.join(self.root, "products"))
        os.makedirs(os.path.join(self.root, "andor-design-system"))
        shutil.copy(os.path.join(REPO, "andor-design-system", "product-registry.json"), os.path.join(self.root, "andor-design-system"))
        self.tool = load_tool()
        self.tool.ROOT = self.root
        self.tool.TEMPLATE = os.path.join(self.root, "shared", "andor-shell", "template")
        self.tool.PRODUCTS = os.path.join(self.root, "products")
        self.tool.REGISTRY = os.path.join(self.root, "andor-design-system", "product-registry.json")

    def run(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = self.tool.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def registry(self):
        with open(self.tool.REGISTRY, encoding="utf-8") as f:
            return json.load(f)

    def files(self):
        found = []
        for directory, _, names in os.walk(self.root):
            for name in names:
                found.append(os.path.relpath(os.path.join(directory, name), self.root))
        return sorted(found)

    def close(self):
        shutil.rmtree(self.root, ignore_errors=True)


GOOD = ["--name", "ANDOR Test Tone", "--slug", "testtone", "--plugin-code", "Atst", "--bundle-id", "com.osp.andortesttone"]


class BootstrapTests(unittest.TestCase):
    def setUp(self):
        self.box = Sandbox()

    def tearDown(self):
        self.box.close()

    def test_creates_a_complete_product(self):
        code, out, err = self.box.run(*GOOD)
        self.assertEqual(code, 0, err)
        folder = os.path.join(self.box.root, "products", "andor-testtone")
        for name in ("CMakeLists.txt", "README.md", "product.json", os.path.join("Source", "Product.cpp")):
            self.assertTrue(os.path.isfile(os.path.join(folder, name)), name)
        with open(os.path.join(folder, "CMakeLists.txt"), encoding="utf-8") as f:
            cmake = f.read()
        self.assertIn('PRODUCT_NAME "ANDOR Test Tone"', cmake)
        self.assertIn("PLUGIN_CODE Atst", cmake)
        self.assertIn("BUNDLE_ID com.osp.andortesttone", cmake)
        self.assertIn("andor_add_instrument(ANDOR_TestTone", cmake)
        with open(os.path.join(folder, "Source", "Product.cpp"), encoding="utf-8") as f:
            product = f.read()
        self.assertIn('"/TONE"', product)                 # the name's last word, in capitals
        self.assertIn('".andortesttone"', product)
        self.assertIn('"ANDOR_TESTTONE"', product)
        self.assertNotIn("{{", product)
        with open(os.path.join(self.box.root, "products", "CMakeLists.txt"), encoding="utf-8") as f:
            self.assertIn("add_subdirectory(andor-testtone)", f.read())
        entry = [p for p in self.box.registry()["products"] if p["slug"] == "testtone"]
        self.assertEqual(len(entry), 1)
        self.assertEqual(entry[0]["pluginCode"], "Atst")
        self.assertTrue(entry[0]["builtFromShell"])

    def test_dry_run_writes_nothing(self):
        before = self.box.files()
        registry = self.box.registry()
        code, out, _ = self.box.run(*GOOD, "--dry-run")
        self.assertEqual(code, 0)
        self.assertIn("nothing written", out)
        self.assertEqual(self.box.files(), before)
        self.assertEqual(self.box.registry(), registry)

    def test_never_overwrites_an_existing_folder(self):
        folder = os.path.join(self.box.root, "products", "andor-testtone")
        os.makedirs(folder)
        keep = os.path.join(folder, "mine.txt")
        with open(keep, "w") as f:
            f.write("user work")
        code, _, err = self.box.run(*GOOD)
        self.assertEqual(code, 2)
        self.assertIn("already exists", err)
        with open(keep) as f:
            self.assertEqual(f.read(), "user work")
        self.assertEqual(os.listdir(folder), ["mine.txt"])

    def test_refuses_every_identifier_another_andor_plugin_uses(self):
        clashes = [
            ["--name", "ANDOR Grain", "--slug", "grain2", "--plugin-code", "Abcd", "--bundle-id", "com.osp.unique"],      # name
            ["--name", "ANDOR New", "--slug", "drums", "--plugin-code", "Abcd", "--bundle-id", "com.osp.unique"],         # slug
            ["--name", "ANDOR New", "--slug", "newone", "--plugin-code", "Osp1", "--bundle-id", "com.osp.unique"],        # OSP's code
            ["--name", "ANDOR New", "--slug", "newone", "--plugin-code", "Abcd", "--bundle-id", "com.osp.andorphrase"],   # Phrase's bundle
        ]
        before = self.box.files()
        for args in clashes:
            code, _, err = self.box.run(*args)
            self.assertEqual(code, 2, args)
            self.assertIn("nothing was written", err, args)
        # A capitalised spelling of an existing bundle ID is refused too (bundle IDs are lower case).
        code, _, _ = self.box.run("--name", "ANDOR New", "--slug", "newone", "--plugin-code", "Abcd", "--bundle-id", "COM.OSP.INSTRUMENT")
        self.assertEqual(code, 2)
        self.assertEqual(self.box.files(), before)

    def test_a_second_product_cannot_take_the_first_ones_identifiers(self):
        self.assertEqual(self.box.run(*GOOD)[0], 0)
        code, _, err = self.box.run("--name", "ANDOR Other", "--slug", "other", "--plugin-code", "Atst", "--bundle-id", "com.osp.andorother")
        self.assertEqual(code, 2)
        self.assertIn("plugin code Atst", err)

    def test_validates_arguments(self):
        bad = [
            ["--name", "Example", "--slug", "example", "--plugin-code", "Aexm", "--bundle-id", "com.osp.x"],            # not ANDOR ...
            ["--name", "ANDOR Ex", "--slug", "Example", "--plugin-code", "Aexm", "--bundle-id", "com.osp.x"],           # slug case
            ["--name", "ANDOR Ex", "--slug", "ex", "--plugin-code", "aexm", "--bundle-id", "com.osp.x"],                # no capital
            ["--name", "ANDOR Ex", "--slug", "ex", "--plugin-code", "Aex", "--bundle-id", "com.osp.x"],                 # 3 chars
            ["--name", "ANDOR Ex", "--slug", "ex", "--plugin-code", "Ae-x", "--bundle-id", "com.osp.x"],                # punctuation
            ["--name", "ANDOR Ex", "--slug", "ex", "--plugin-code", "Aexm", "--bundle-id", "osp"],                      # bundle form
            ["--name", "ANDOR Ex", "--slug", "ex", "--plugin-code", "Aexm", "--bundle-id", "com.osp.x", "--version", "1.0"],
            ["--name", "ANDOR Ex", "--slug", "ex", "--plugin-code", "Aexm", "--bundle-id", "com.osp.x", "--identity", "TOOLONGNAME"],
        ]
        before = self.box.files()
        for args in bad:
            code, _, _ = self.box.run(*args)
            self.assertEqual(code, 2, args)
        self.assertEqual(self.box.files(), before)

    def test_only_the_new_files_are_touched(self):
        # Nothing outside products/andor-<slug>/, products/CMakeLists.txt and the registry.
        before = set(self.box.files())
        self.assertEqual(self.box.run(*GOOD)[0], 0)
        added = set(self.box.files()) - before
        for path in added:
            self.assertTrue(path.startswith(os.path.join("products", "andor-testtone")) or path == os.path.join("products", "CMakeLists.txt"), path)


if __name__ == "__main__":
    unittest.main(verbosity=1)
