# Copyright 2026 Ivan Kniazkov
"""Shared compile/verified-module execution fixture for multi-file runtime tests."""
import json
import os
from pathlib import Path
import subprocess


def put(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding='utf-8')
    return path


class ExecutionSuite:
    def __init__(self, executable, root):
        self.exe = str(Path(executable).resolve())
        self.root = Path(root)
        self.unrelated = self.root / 'unrelated'
        self.unrelated.mkdir()
        self.count = 0

    def project(self, name):
        return Project(self, name)


class Project:
    def __init__(self, suite, name):
        self.suite = suite
        self.root = suite.root / name
        self.modules = self.root / 'modules'
        self.modules.mkdir(parents=True)
        self.environment = dict(os.environ, GOAT_MODULE_PATH=str(self.modules))

    def invoke(self, mode, path, output=None, expected='', fail=False):
        self.suite.count += 1
        command = [self.suite.exe, mode, str(path)]
        if output is not None:
            command.append(str(output))
        result = subprocess.run(command, capture_output=True, timeout=60,
                                cwd=self.suite.unrelated, env=self.environment)
        assert (result.returncode != 0) if fail else (result.returncode == 0), (
            self.suite.count, command, result.returncode, result.stdout, result.stderr)
        actual = result.stdout.decode('utf-8').replace('\r\n', '\n')
        assert actual == expected, (self.suite.count, command, actual, expected, result.stderr)
        if fail:
            assert result.stderr, (self.suite.count, 'missing diagnostic', command)
        return result

    def compile(self, name, code, version='1', files=None, defaults=None, fail=False):
        directory = self.root / 'sources' / (name + '-' + version)
        source = put(directory / 'main.goat', code)
        for filename, content in (files or {}).items():
            put(directory / filename, content)
        descriptor = {'formatVersion': 1, 'name': name, 'version': version,
                      'main': 'main.goat'}
        if defaults is not None:
            descriptor['dependencies'] = defaults
        source = put(directory / 'module.gmod', json.dumps(descriptor))
        artifact = self.root / 'app.gbin' if name == 'app' else self.modules / name / (version + '.gbin')
        artifact.parent.mkdir(parents=True, exist_ok=True)
        self.invoke('compile', source, artifact, fail=fail)
        if fail:
            assert not artifact.exists()
        return artifact

    def run(self, code, expected, **kwargs):
        artifact = self.compile('app', code, **kwargs)
        self.invoke('binary', artifact, expected=expected)
        return artifact

