#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Exercise the actual release archive outside the checkout (Python stdlib only)."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def check(archive):
    archive = Path(archive).resolve()
    with tempfile.TemporaryDirectory(prefix="goat package ") as temporary:
        workspace = Path(temporary)
        unpacked = workspace / "unpacked"
        shutil.unpack_archive(str(archive), str(unpacked))
        roots = list(unpacked.iterdir())
        assert len(roots) == 1 and roots[0].is_dir(), roots
        package = workspace / "relocated package"
        roots[0].rename(package)
        unpacked.rmdir()
        exe = package / "bin" / ("goat.exe" if os.name == "nt" else "goat")
        for name in ("include/goat/native_abi.h", "include/goat/native_runtime.h",
                     "README.md", "LICENSE.txt", "build-info.txt", "docs/native-memory.md"):
            assert (package / name).is_file(), name
        work = workspace / "user programs"
        work.mkdir()
        environment = os.environ.copy()
        for key in ("CPATH", "C_INCLUDE_PATH", "LIBRARY_PATH", "LD_LIBRARY_PATH"):
            environment.pop(key, None)
        isolated = environment.copy()
        isolated["CC"] = str(workspace / "no compiler")
        isolated["PATH"] = (str(Path(os.environ["SystemRoot"]) / "System32")
                            if os.name == "nt" else "")

        def run(*args, env=environment, expected=None):
            result = subprocess.run([str(exe), "--lang", "en", *map(str, args)],
                                    cwd=work, env=env, text=True, capture_output=True,
                                    timeout=90)
            assert result.returncode == 0, (args, result.returncode, result.stdout, result.stderr)
            if expected is not None:
                assert result.stdout.strip() == expected, result.stdout
            return result.stdout

        run("--native", "off", package / "examples/hello_world.goat", env=isolated)
        source = work / "numeric.goat"
        shutil.copyfile(package / "examples/numeric.goat", source)
        run("--native", "off", source, env=isolated, expected="55")
        run("--compile", "--optimize", "none", source, env=isolated, expected="")
        source.rename(work / "saved source")
        run("--run", "--native", "off", source.with_suffix(".gbin"), env=isolated, expected="55")
        (work / "saved source").rename(source)

        # --print-c needs only the installed public headers, without exported copies.
        generated = work / "printed.c"
        generated.write_text(run("--print-c", source), encoding="utf-8")
        compiler = environment.get("CC", "gcc")
        subprocess.run([compiler, "-std=c11", "-O2", "-fno-fast-math", "-ffp-contract=off",
                        "-I" + str(package / "include/goat"), "-c", str(generated),
                        "-o", str(work / "printed.o")], cwd=work, env=environment,
                       check=True, timeout=90)
        run("--save-c", source, expected="")
        for name in ("native_abi.h", "native_runtime.h", "native_strings.h"):
            assert (work / name).read_bytes() == (package / "include/goat" / name).read_bytes()
            (work / name).unlink()
        source.with_suffix(".c").unlink()
        generated.unlink()
        run("--compile", "--native", "required", source, expected="")
        library = source.with_suffix(".dll" if os.name == "nt" else ".so")
        assert library.is_file(), library
        source.unlink()
        # Move the compiled pair once more; neither source nor headers are available here.
        deployed = workspace / "deployed program"
        deployed.mkdir()
        binary = source.with_suffix(".gbin")
        shutil.move(str(binary), deployed / binary.name)
        shutil.move(str(library), deployed / library.name)
        report = work / "native report.txt"
        run("--run", "--native", "required", "--save-native", report,
            deployed / binary.name, env=isolated, expected="55")
        contents = report.read_text(encoding="utf-8")
        assert "preparation=ready" in contents, contents
        assert re.search(r"succeeded=[1-9][0-9]*", contents), contents
        print(f"Package OK: {archive.name}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: check_package.py <archive-or-packages-directory>")
    argument = Path(sys.argv[1])
    archives = sorted(argument.glob("*.zip")) + sorted(argument.glob("*.tar.gz")) if argument.is_dir() else [argument]
    if not archives:
        sys.exit("No release archives found")
    for archive in archives:
        check(archive)
