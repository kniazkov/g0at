"""Explicit export recipes for the pre-migration structured Python random inputs.

Never imported by ordinary tests. The central exporter calls samples(); these
recipes preserve the original call order, including type iteration and shuffle
starting order, rather than substituting another PRNG with the same seed.
"""
import math
import random
import struct


SOURCE_SHA256 = {
    'scripts/check_module_abi.py':
        '920bc4c154e76ca73b85e3c186b26b5b205e28da70d24f5526ae008748968a8e',
    'scripts/check_module_canonical.py':
        'b9ead65fc2daf8389f55ab11361dc6eec0cbe982596e7821bf0b3b3c3ed3e0fc',
    'test/src/native/checks/saturation.py':
        'c8d65747b5c7c7627422a96073f205478aa0556aa0e6544295e79af196842a3b',
}

METADATA = {
    'abi-guard-pairs.json': {
        'source': 'scripts/check_module_abi.py',
        'scenario': 'seeded differential guard overlap',
        'seed': 209, 'count': 600,
        'element': 'pair of guard descriptors (100 pairs per scalar type)',
    },
    'canonical-key-orders.json': {
        'source': 'scripts/check_module_canonical.py',
        'scenario': 'main-metadata object key permutations',
        'seed': 180, 'count': 20,
        'element': 'ordered list of the 13 enriched metadata keys',
    },
    'saturation-pairs.json': {
        'source': 'test/src/native/checks/saturation.py',
        'original_source': 'test/native/checks/saturation.py',
        'scenario': 'independent exact arithmetic helper comparisons',
        'seed': 20261006, 'count': 1000,
        'element': 'pair of signed 64-bit operands (boundary pairs remain in the test)',
    },
}


def samples():
    # scripts/check_module_abi.py: values, predicate and the original random loop.
    values = {'integer': [-2**63, 2**63-1, -1, 0, 1],
              'real': [struct.pack('>d', x).hex() for x in
                       (-math.inf, -0.0, 0.0, 1.0, math.inf, math.nan, math.nextafter(0, 1))],
              'character': ['\0', 'A', '\ud7ff', '\ue000', '\U0010ffff'],
              'boolean': [False, True], 'null': [None], 'string': ['', 'A\0😀', 'A\0😁']}

    def predicate(kind, value, operation='equal'):
        return dict(argument=0, operation=operation, constant=dict(type=kind, value=value))

    rng = random.Random(209)
    pairs = []
    for kind, constants in values.items():
        operations = ['equal', 'notEqual']
        if kind in ('integer', 'real', 'character'):
            operations += ['less', 'lessEqual', 'greater', 'greaterEqual']
        for _ in range(100):
            pair = []
            for _ in range(2):
                pair.append(dict(argumentTypes=[kind], conditions=[
                    predicate(kind, rng.choice(constants), rng.choice(operations))
                    for _ in range(rng.randrange(6))]))
            pairs.append(pair)

    # The order is BASE, module_with's functions, then enriched.update's keys.
    # Every original iteration starts with a fresh enriched.items() list.
    enriched_keys = ['formatVersion', 'bytecodeVersion', 'name', 'version', 'mainHash',
                     'main', 'files', 'functions', 'description', 'authors',
                     'dependencies', 'externals', 'producers']
    rng = random.Random(180)
    orders = []
    for _ in range(20):
        keys = list(enriched_keys)
        rng.shuffle(keys)
        orders.append(keys)

    MIN = -(1 << 63)
    MAX = (1 << 63) - 1
    rng = random.Random(20261006)
    operands = [(rng.randint(MIN, MAX), rng.randint(MIN, MAX)) for _ in range(1000)]
    return {'abi-guard-pairs.json': pairs,
            'canonical-key-orders.json': orders,
            'saturation-pairs.json': operands}
