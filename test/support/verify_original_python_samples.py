"""Optional migration audit against the original committed Python recipes.

Requires Git history, unlike ordinary tests and the regular fixture --check.
Run from the repository root with --baseline 89b8d80ba13dfa3cb5ca9d95b1448d45fec438f2.
Only selected original AST nodes execute: no production host or expected oracle.
"""
import argparse
import ast
import json
import math
import random
import struct
import subprocess
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from python_samples_structured import samples
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', required=True, help='original pre-freeze Git commit')
BASELINE = parser.parse_args().baseline
def source(path):
    return subprocess.check_output(['git','show',BASELINE+':'+path], text=True)
def execute(nodes,ns):
    exec(compile(ast.Module(body=nodes,type_ignores=[]), '<original-baseline>', 'exec'), ns)
def assigns(n,name):
    return isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets)
frozen=json.loads(json.dumps(samples()))
ns=dict(random=random,math=math,struct=struct)
tree=ast.parse(source('scripts/check_module_abi.py'))
execute([n for n in tree.body if assigns(n,'values') or isinstance(n,ast.FunctionDef) and n.name=='predicate'], ns)
a=next(i for i,n in enumerate(tree.body) if assigns(n,'rng'))
execute(tree.body[a:a+3],ns)
assert ns['pairs']==frozen['abi-guard-pairs.json']
print('600 ABI pairs equal original AST loop; 1200 guards with all constants and operations')
tree=ast.parse(source('scripts/check_module_canonical.py'));ns=dict(random=random)
execute([n for n in tree.body if assigns(n,'BASE') or isinstance(n,ast.FunctionDef) and n.name=='module_with'],ns)
body=next(n.body for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='main')
execute([n for n in body if assigns(n,'pure')],ns)
a=next(i for i,n in enumerate(body) if assigns(n,'enriched')); execute(body[a:a+2],ns)
a=next(i for i,n in enumerate(body) if assigns(n,'rng'))
# Replace only original assertion (production call) by recording shuffled keys.
loop=body[a+1];loop.body[-1]=ast.parse('orders.append([key for key, value in pairs])').body[0]
ns['orders']=[];execute(body[a:a+2],ns)
assert ns['orders']==frozen['canonical-key-orders.json']
print('20 key permutations equal original AST loop (all 13 keys each)')
tree=ast.parse(source('test/src/native/checks/saturation.py'));ns=dict(random=random,pairs=[])
execute([n for n in tree.body if assigns(n,'MIN') or assigns(n,'MAX')],ns)
body=next(n.body for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='main')
a=next(i for i,n in enumerate(body) if assigns(n,'rng'));execute(body[a:a+2],ns)
assert [list(x) for x in ns['pairs']]==frozen['saturation-pairs.json']
print('1000 operand pairs equal original AST loop; exact int64 values retained')

# Byte inputs and continued RNG streams, executing the original AST loops.
import copy
import io
import warnings
import types
import zipfile
from python_samples_binary import samples as binary_samples
binary = binary_samples()
tree = ast.parse(source('scripts/check_module_writer.py'))
ns = dict(random=random, copy=copy)
a = next(i for i, n in enumerate(tree.body) if assigns(n, 'rng'))
execute(tree.body[a:a+2], ns)
assert ns['contents'][-1] == binary['writer-payload.bin']
ns['m'] = {'native': list(range(12))}
a = next(i for i, n in enumerate(tree.body) if assigns(n, 'shuffled'))
execute(tree.body[a:a+2], ns)
assert ns['shuffled']['native'] == binary['writer-native-order.json']
print('65537 writer bytes and continued 12-index shuffle equal original AST')

for kind, script, base_name in [('zip', 'scripts/check_zip.py', 'deflated'),
                              ('container', 'scripts/check_module_container.py', 'base')]:
    tree = ast.parse(source(script))
    root = Path(__file__).resolve().parents[2]
    vectors = root / 'docs/module-format-v1/vectors'
    ns = dict(random=random, io=io, warnings=warnings, zipfile=zipfile, json=json,
              BASE=json.loads((vectors / 'empty.module.json').read_text()),
              BYTECODE=(vectors / 'empty.bytecode.bin').read_bytes())
    if kind == 'zip':
        class UnixZipInfo(zipfile.ZipInfo):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                self.create_system = 3
        ns['zipfile'] = types.SimpleNamespace(ZipInfo=UnixZipInfo, ZipFile=zipfile.ZipFile)
    execute([n for n in tree.body if isinstance(n, ast.FunctionDef)
             and n.name in ('independent', 'pack', 'parts')], ns)
    execute([n for n in tree.body if assigns(n, base_name)], ns)
    a = next(i for i, n in enumerate(tree.body) if assigns(n, 'rng'))
    if kind == 'zip':
        execute(tree.body[a:a+2], ns)
        assert ns['contents'][-1] == binary['zip-payload.bin']
        loop = next(n for n in tree.body if isinstance(n, ast.For)
                    and isinstance(n.iter, ast.Call) and ast.unparse(n.iter) == 'range(60)')
    else:
        execute(tree.body[a:a+1], ns)
        loop = tree.body[a+1]
    captured = []
    ns['check'] = lambda data, *args: captured.append(data)
    ns['run'] = lambda mode, data, *args: captured.append(data)
    execute([loop], ns)
    def compare(base, actual):
        expected = []
        for mutations in binary[kind + '-damage.json']:
            data = bytearray(base)
            for offset, mask in mutations:
                data[offset] ^= mask
            expected.append(bytes(data))
        assert actual == expected
    assert ns[base_name] == binary[kind + '-damage-base.bin']
    compare(ns[base_name], captured)
    if kind == 'zip':
        # Windows original differs only in ZipInfo's platform default. Reexecute
        # the unchanged builder with that documented stdlib default, not a new RNG.
        class WindowsZipInfo(zipfile.ZipInfo):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                self.create_system = 0
        ns['zipfile'] = types.SimpleNamespace(ZipInfo=WindowsZipInfo, ZipFile=zipfile.ZipFile)
        execute([n for n in tree.body if assigns(n, base_name)], ns)
        execute(tree.body[a:a+2], ns)
        captured.clear()
        execute([loop], ns)
        assert ns[base_name] == binary['zip-damage-base-windows.bin']
        compare(ns[base_name], captured)
    print('%s original damaged archives byte-identical (%d cases)' % (kind, len(captured)))
