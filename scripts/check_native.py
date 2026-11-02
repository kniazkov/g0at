#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Temporary execution/output compatibility for string_method_errors.py (#385).

The manifest runner and self-checks are implemented in C; this is not a runnable suite.
Remove this compatibility module when its final consumer is migrated.
"""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def normalized(data):
    # Windows text output uses CRLF. Do not trim whitespace, NULs or empty lines.
    return data.replace(b'\r\n', b'\n')


def compare_output(actual, expected, label):
    require(normalized(actual) == normalized(expected), f'{label}: output differs; see saved stdout files')


def execute(command, work, label, env=None):
    (work / (label + '.command.json')).write_text(json.dumps(command, ensure_ascii=False), encoding='utf-8')
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, env=env, timeout=300)
    except subprocess.TimeoutExpired as error:
        (work / (label + '.stdout')).write_bytes(error.stdout or b'')
        (work / (label + '.stderr')).write_bytes(error.stderr or b'')
        raise
    (work / (label + '.stdout')).write_bytes(result.stdout)
    (work / (label + '.stderr')).write_bytes(result.stderr)
    require(result.returncode == 0, f'{label}: exit {result.returncode}; see {work / (label + ".stderr")}')
    return result
