#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Temporary #391 gate: independent original/C runs on identical archives."""
import ast
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys


def validate(root):
    original = root / "scripts/check_package.py"
    fixture = root / "test/data/package/sdk.c"
    provenance = (fixture.parent / "README.md").read_text(encoding="utf-8")
    for path in (original, fixture):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if f"`{digest}`" not in provenance:
            raise RuntimeError(f"Frozen source hash mismatch: {path}")
    tree = ast.parse(original.read_text(encoding="utf-8"))
    literals = [node.args[0].value for node in ast.walk(tree)
                if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
                and isinstance(node.func.value, ast.Name)
                and node.func.value.id == "probe_source" and node.func.attr == "write_text"
                and node.args and isinstance(node.args[0], ast.Constant)]
    if literals != [fixture.read_text(encoding="utf-8")]:
        raise RuntimeError("SDK fixture does not match original AST literal")
    return original


def main():
    if len(sys.argv) != 7:
        raise RuntimeError("Usage: check_package_parity.py ARCHIVES C_RUNNER DELIVERY CC SOURCE_ROOT CMAKE")
    argument, runner, delivery, compiler, root, cmake = sys.argv[1:]
    compiler = shutil.which(compiler)
    if compiler is None:
        raise RuntimeError("C compiler not found")
    runner = str(Path(runner).resolve())
    delivery = str(Path(delivery).resolve())
    root = Path(root).resolve()
    original = validate(root)
    argument = Path(argument).resolve()
    archives = (sorted(argument.glob("*.zip")) + sorted(argument.glob("*.tar.gz"))
                if argument.is_dir() else [argument])
    if not archives or any(not archive.is_file() for archive in archives):
        raise RuntimeError("No release archives found")
    environment = os.environ.copy()
    environment["CC"] = compiler
    for archive in archives:
        before = hashlib.sha256(archive.read_bytes()).digest()
        subprocess.run([sys.executable, str(original), str(archive), delivery],
                       env=environment, check=True, timeout=1800)
        if hashlib.sha256(archive.read_bytes()).digest() != before:
            raise RuntimeError("Original modified produced package archive")
        subprocess.run([runner, "integration.package.end-to-end", str(archive), delivery,
                        compiler, str(root), cmake, "--timeout-seconds", "1800"],
                       env=environment, check=True, timeout=1830)
        if hashlib.sha256(archive.read_bytes()).digest() != before:
            raise RuntimeError("C runner modified produced package archive")
        print(f"Original/C package parity OK: {archive.name}", flush=True)


if __name__ == "__main__":
    main()
