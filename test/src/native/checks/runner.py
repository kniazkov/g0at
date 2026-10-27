#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
"""Check that the differential runner rejects false-positive native results."""
import contextlib
import importlib.util
import io
import sys
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location('check_native', ROOT / 'scripts/check_native.py')
runner = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(runner)


class NativeRunnerChecks(unittest.TestCase):
    def test_output_comparison_preserves_data(self):
        runner.compare_output(b'a\r\nb\x00\r\n', b'a\nb\x00\n', 'same text')
        for wrong in (b'a\nb\n', b'a\nb\x00', b'a\nb\x00 \n', b'a\nb\x00\n\n'):
            with self.subTest(output=wrong), self.assertRaises(AssertionError):
                runner.compare_output(wrong, b'a\nb\x00\n', 'changed text')

    def test_successful_output_cannot_mask_missing_native_execution(self):
        good = dict(mode='required', preparation='ready', bound='2',
                    attempts='3', succeeded='3', retries='0')
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / 'native.report'

            def check(fields, native=True):
                report.write_text(''.join(f'{key}={value}\n' for key, value in fields.items()), encoding='utf-8')
                runner.check_report(report, {'calls': 3, 'bound': 2}, native)

            check(good)
            for change in [dict(mode='auto'), dict(preparation='empty'), dict(bound='0'),
                           dict(bound='1'), dict(attempts='0', succeeded='0'),
                           dict(attempts='4'), dict(succeeded='2'), dict(retries='1')]:
                with self.subTest(change=change), self.assertRaises(AssertionError):
                    check(good | change)
            for key in good:
                with self.subTest(missing=key), self.assertRaises(AssertionError):
                    check({k: v for k, v in good.items() if k != key})
            check(dict(mode='off', attempts='0', succeeded='0', retries='0'), native=False)
            with self.assertRaises(AssertionError):
                check(dict(mode='off', attempts='1', succeeded='1', retries='0'), native=False)

    def test_failure_does_not_skip_later_cases(self):
        visited = []

        def run_case(exe, case, work):
            visited.append(case['name'])
            if case['name'] == 'bad':
                raise AssertionError('deliberate output mismatch')

        with tempfile.TemporaryDirectory() as temporary, contextlib.redirect_stdout(io.StringIO()):
            with patch.object(runner, 'load_cases', return_value=[{'name': 'bad'}, {'name': 'good'}]), \
                 patch.object(runner, 'program_case', side_effect=run_case), \
                 patch.object(sys, 'argv', ['check_native.py', __file__, temporary]):
                self.assertEqual(runner.main(), 1)
        self.assertEqual(visited, ['bad', 'good'])


if __name__ == '__main__':
    unittest.main(argv=[__file__])
