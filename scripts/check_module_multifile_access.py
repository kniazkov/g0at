#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""AC-01–AC-28 through real include/import, analyzer, VM and public CLI (#218)."""
from pathlib import Path
import subprocess
import sys
import tempfile

from module_execution_support import ExecutionSuite, Project
from module_access_basic_cases import run as basics
from module_access_copy_cases import run as copies
from module_access_boundary_cases import run as boundaries


class AccessProject(Project):
    def invoke(self, mode, path, output=None, expected='', fail=False):
        result = super().invoke(mode, path, output, expected, fail)
        if mode == 'binary':
            # Repeat source graph discovery and binary-only execution using the
            # public CLI, outside the project directory, with no native bypass.
            sources = [self.root / 'sources' / 'app-1' / 'module.gmod', path]
        elif mode == 'compile' and fail:
            # Proven denials must prevent all execution, not become catchable
            # runtime failures under a different optimizer setting.
            sources = [path]
        else:
            return result
        for source in sources:
            for optimization in ('none', 'all'):
                command = [self.suite.goat, '--native', 'off', '--optimize', optimization,
                           str(source)]
                actual = subprocess.run(command, capture_output=True, timeout=60,
                                        cwd=self.suite.unrelated, env=self.environment)
                self.suite.cli_count += 1
                assert (actual.returncode != 0) if fail else actual.returncode == 0, (
                    command, actual.returncode, actual.stdout, actual.stderr)
                assert actual.stdout.decode('utf-8').replace('\r\n', '\n') == expected, (
                    command, actual.stdout, expected, actual.stderr)
                if fail:
                    assert actual.stderr, (command, 'missing diagnostic')
                    for diagnostic in (b'Access denied: read of x',
                                       b'Exactly one access modifier',
                                       b'Expected name with value assignment'):
                        if diagnostic in result.stderr:
                            assert diagnostic in actual.stderr, (command, actual.stderr)
        return result


class AccessSuite(ExecutionSuite):
    def __init__(self, executable, goat, root):
        super().__init__(executable, root)
        self.goat = str(Path(goat).resolve())
        self.cli_count = 0

    def project(self, name):
        return AccessProject(self, name)


with tempfile.TemporaryDirectory(prefix='goat-multifile-access-') as temporary:
    suite = AccessSuite(sys.argv[1], sys.argv[2], temporary)
    basics(suite)
    copies(suite)
    boundaries(suite)

print(f'Multifile access AC-01–AC-28: {suite.count} compile/VM checks, '
      f'{suite.cli_count} public CLI checks passed')
