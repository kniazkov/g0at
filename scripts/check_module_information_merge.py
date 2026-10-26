#!/usr/bin/env python3
# Copyright 2026 Ivan Kniazkov
"""Exercise production all-input metadata merging and interactive author ordering."""
import copy
import itertools
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXE = str(Path(sys.argv[1]).resolve())
M = json.loads((ROOT / 'docs/module-format-v1/vectors/empty.module.json').read_text())
COUNT = 0


def module(**fields):
    return dict(copy.deepcopy(M), **fields)


def run(inputs, responses=(), interactive=False, bad=False, raw=None):
    global COUNT
    envelope = dict(inputs=inputs, responses=list(responses), interactive=interactive)
    data = raw if raw is not None else json.dumps(envelope, ensure_ascii=False).encode()
    p = subprocess.run([EXE, '--information-merge'], input=data,
                       capture_output=True, timeout=10)
    COUNT += 1
    assert p.returncode == (1 if bad else 0), (COUNT, envelope, p.returncode, p.stderr)
    if bad:
        assert not p.stdout and p.stderr, (COUNT, p)
        return p
    return json.loads(p.stdout), p.stderr


def merge(inputs, **kwargs):
    result, diagnostic = run(inputs, **kwargs)
    return result


# Zero sources is invalid, but an information-free source is valid.
run([], bad=True)
assert merge([M]) == {}
assert merge([M, M, M]) == {}
for key in ('description', 'license', 'homepage', 'authors', 'producers'):
    empty = [] if key in ('authors', 'producers') else ''
    for inputs in ([M, module(**{key: empty})], [module(**{key: empty}), M]):
        assert merge(inputs) == {}
    for invalid in (None, True, 1, {}, '' if isinstance(empty, list) else []):
        run([M, module(**{key: invalid})], bad=True)

for key in ('description', 'license', 'homepage'):
    for text in (' ', '\t\r\n', 'Ёж 😀', 'é', 'e\u0301', 'a\0b', 'a\0c', 'https://127.0.0.1:1'):
        same = module(**{key: text})
        assert merge([same, same, same]) == {key: text}
        for inputs in ([same, M], [M, same]):
            run(inputs, bad=True)
        env = dict(inputs=[same, same], responses=[], interactive=False)
        assert run([], raw=json.dumps(env, ensure_ascii=True).encode())[0] == {key: text}
    for left, right in [('A', 'a'), ('a', 'a '), ('é', 'e\u0301'), ('a\0b', 'a\0c'),
                        ('\n', '\r\n')]:
        run([module(**{key: left}), module(**{key: right})], bad=True)

# Literal and escaped strings decode to the same values within a single request.
raw = json.dumps(dict(inputs=[module(description='a'), module(description='a')],
                      responses=[], interactive=False)).encode()
raw = raw.replace(b'"description": "a"', b'"description": "\\u0061"', 1)
assert run([], raw=raw)[0] == {'description': 'a'}

A = dict(name='Иван')
B = dict(name='Анна', email='a@example.invalid')
C = dict(name='Иван', email='other')
for inputs in ([module(authors=[B, A])], [M, module(authors=[B, A]), M],
               [module(authors=[B, A]), module(authors=[])]):
    result, diagnostic = run(inputs)
    assert result == {'authors': [B, A]} and not diagnostic
assert merge([module(authors=[dict(name='Иван', email='')])]) == {'authors': [A]}
for inputs in ([module(authors=[A]), module(authors=[A])],
               [module(authors=[A, B]), module(authors=[B, A])],
               [module(authors=[A]), M, module(authors=[B])],
               [module(authors=[A, B]), module(authors=[B, C])]):
    run(inputs, bad=True)
    run(inputs, interactive=True, bad=True)  # Immediate EOF.

sources = [module(authors=[A, B]), M, module(authors=[B, C])]
assert merge(sources, interactive=True, responses=['3 1 2']) == {'authors': [C, A, B]}
assert merge([module(authors=[A]), module(authors=[A])],
             interactive=True, responses=['1']) == {'authors': [A]}
for response in ('', '1 2', '1 1 2', '0 1 2', '1 2 4', '1 2 text', '-1 2 3',
                 '1 2 3 4', '1.0 2 3', '999999999999999999999999999999 2 3', '1\0 2 3'):
    run(sources, interactive=True, responses=[response], bad=True)
    result, diagnostic = run(sources, interactive=True, responses=[response, '3 1 2'])
    assert result == {'authors': [C, A, B]} and diagnostic
    assert diagnostic.count(b'1. {') == 1 and diagnostic.count(b'2. {') == 1
    assert diagnostic.count(b'3. {') == 1
    assert diagnostic.count(b'Choose author order') == 2
assert merge(sources, interactive=True, responses=['', '1', '1 1 1', '2 3 1']) == {
    'authors': [B, C, A]}
assert merge(sources, interactive=True, responses=['  3  1 2  ']) == {'authors': [C, A, B]}
assert merge(sources, interactive=True, responses=[' ' * 300 + '3 1 2']) == {
    'authors': [C, A, B]}
# Identity includes all bytes and both fields; no implicit Unicode/name/email folding.
authors = [A, C, dict(name='Пётр', email='other'), dict(name='Иван '),
           dict(name='é'), dict(name='e\u0301'), dict(name='n\0x'), dict(name='n\0y')]
assert merge([module(authors=authors)]) == {'authors': authors}
result, display = run([module(authors=authors), module(authors=authors)],
                      interactive=True, responses=['8 7 6 5 4 3 2 1'])
assert result == {'authors': list(reversed(authors))}
assert b'\\u0000' in display and 'Пётр'.encode() in display and b'other' in display
for bad_authors in ([A, A], [A, dict(name='Иван', email='')], [dict(name='')], [{}],
                    [dict(name='x', email=None)], [dict(name='x', extra='y')], [None]):
    run([module(authors=bad_authors)], bad=True)

P = dict(name='Compiler', version='10', buildId='a')
Q = dict(name='Compiler', version='2', buildId='a')
R = dict(name='Compiler', version='2', buildId='b')
S = dict(name='Компилятор', version='x', buildId='\0id')
producers = [P, Q, R, S]
for inputs in itertools.permutations([module(producers=[Q, P]),
                                     module(producers=[R, Q]), module(producers=[S])]):
    assert merge(inputs) == {'producers': producers}
assert merge([module(producers=producers), module(producers=producers)]) == {
    'producers': producers}
assert merge([module(authors=[A], producers=[P]), module(producers=[Q])]) == {
    'authors': [A], 'producers': [P, Q]}
# Producer identity is decoded and length-aware for every component.
for key in P:
    values = ['a', 'A', 'a ', 'a\0b', 'a\0c', 'é', 'e\u0301']
    variants = [dict(P, **{key: value}) for value in values]
    expected = sorted(variants, key=lambda item: tuple(item[k].encode() for k in P))
    assert merge([module(producers=variants[:3]), module(producers=variants[3:])]) == {
        'producers': expected}
raw = json.dumps(dict(inputs=[module(producers=[P]), module(producers=[P])],
                      responses=[], interactive=False)).encode()
raw = raw.replace(b'Compiler', b'\\u0043ompiler', 1)
assert run([], raw=raw)[0] == {'producers': [P]}
for bad_producers in ([P, P], [None], [dict(P, extra='x')], [{}]):
    run([module(producers=bad_producers)], bad=True)
for key in P:
    for value in (None, '', False, 1, [], {}):
        run([module(producers=[dict(P, **{key: value})])], bad=True)
    record = dict(P); del record[key]
    run([module(producers=[record])], bad=True)

# Invalid late input must fail before any interaction, even after valid author lists.
for invalid in (module(version=None), module(unknown='x'), module(authors=[A, A])):
    p = run([module(authors=[A]), module(authors=[B]), invalid],
            interactive=True, responses=['2 1'], bad=True)
    assert b'Choose author order' not in p.stderr and b'1. {' not in p.stderr, p.stderr
p = run([module(description='x'), module(description='y'), module(version=None)],
        interactive=True, responses=['1'], bad=True)
assert b'invalid module information input' in p.stderr
# Conflicts also precede interactive resolution.
run([module(authors=[A], description='x'), module(authors=[B], description='y')],
    interactive=True, responses=['2 1'], bad=True)
print(f'[ ok ] module information merge: {COUNT} cases')
