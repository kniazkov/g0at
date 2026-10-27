"""Explicit reproduction of the pre-#330 Python random inputs; not test oracles.

Generation order is significant: ZIP damage and writer permutation continue the
same Random stream after consuming 65,537 payload bytes.
"""
import hashlib
import io
import json
from pathlib import Path
import random
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[2]
VECTORS = ROOT / 'docs/module-format-v1/vectors'
BASE = json.loads((VECTORS / 'empty.module.json').read_text())
BYTECODE = (VECTORS / 'empty.bytecode.bin').read_bytes()

def independent(parts, stream=False, create_system=3):
    class NonSeek(io.BytesIO):
        def seekable(self):
            return False
        def seek(self, *args):
            raise io.UnsupportedOperation('non-seekable')
    out = NonSeek() if stream else io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', UserWarning)
        with zipfile.ZipFile(out, 'w') as archive:
            archive.comment = b'archive\0comment'
            for name, content, method in parts:
                zi = zipfile.ZipInfo(name, (2001, 2, 3, 4, 5, 6))
                zi.create_system = create_system
                zi.compress_type = method
                zi.extra = b'\xfe\xca\x03\x00abc'
                zi.comment = b'file\0comment'
                archive.writestr(zi, content)
    return out.getvalue()

def pack(parts, stream=False, extra=b'', comment=b'', attributes=None):
    class NonSeek(io.BytesIO):
        def seekable(self):
            return False
        def seek(self, *args):
            raise io.UnsupportedOperation('stream')
    output = NonSeek() if stream else io.BytesIO()
    with warnings.catch_warnings():
        warnings.simplefilter('ignore', UserWarning)
        with zipfile.ZipFile(output, 'w') as z:
            z.comment = comment
            for name, data, method in parts:
                info = zipfile.ZipInfo(name, (2001, 2, 3, 4, 5, 6))
                info.compress_type = method
                info.create_system = 3
                info.external_attr = (0o40755 << 16 | 0x10) if name.endswith('/') else 0o100640 << 16
                if attributes and name in attributes:
                    info.external_attr = attributes[name]
                info.extra = extra
                info.comment = b'entry\0comment'
                z.writestr(info, data)
    return output.getvalue()

def parts(metadata=BASE, bytecode=BYTECODE, native=(), method=8):
    return [('module.json', json.dumps(metadata, ensure_ascii=False, indent=2).encode(), method),
            ('bytecode.bin', bytecode, method), *native]


def samples():
    result = {}
    rng = random.Random(183)
    result['writer-payload.bin'] = bytes(rng.randrange(256) for _ in range(65537))
    order = list(range(12))
    rng.shuffle(order)
    result['writer-native-order.json'] = order
    rng = random.Random(247)
    result['zip-payload.bin'] = bytes(rng.randrange(256) for _ in range(65537))
    base = independent([('x', b'a'*10000, 8)])
    result['zip-damage-base.bin'] = base
    result['zip-damage-base-windows.bin'] = independent([('x', b'a'*10000, 8)], create_system=0)
    result['zip-damage.json'] = [
        [[rng.randrange(len(base)), 1 << rng.randrange(8)] for _ in range(3)]
        for _ in range(60)]
    rng = random.Random(182)
    base = pack(parts())
    result['container-damage-base.bin'] = base
    result['container-damage.json'] = [
        [[rng.randrange(len(base)), 1 << rng.randrange(8)] for _ in range(3)]
        for _ in range(100)]
    return result


METADATA = {
    'writer-payload.bin': dict(source='scripts/check_module_writer.py', scenario='native incompressible payload', seed=183, count=65537, unit='byte'),
    'writer-native-order.json': dict(source='scripts/check_module_writer.py', scenario='native permutation after 65537 byte draws', seed=183, count=12, unit='index'),
    'zip-payload.bin': dict(source='scripts/check_zip.py', scenario='incompressible ZIP entry', seed=247, count=65537, unit='byte'),
    'zip-damage-base.bin': dict(source='scripts/check_zip.py', scenario='independent deflated base for frozen damage', seed=None, count=1, unit='archive'),
    'zip-damage-base-windows.bin': dict(source='scripts/check_zip.py', scenario='Windows ZipInfo create_system=0 independent deflated base', seed=None, count=1, unit='archive'),
    'zip-damage.json': dict(source='scripts/check_zip.py', scenario='damage after 65537 byte draws; three ordered offset/xor-mask pairs per case', seed=247, count=60, unit='case'),
    'container-damage-base.bin': dict(source='scripts/check_module_container.py', scenario='independent module base for frozen damage', seed=None, count=1, unit='archive'),
    'container-damage.json': dict(source='scripts/check_module_container.py', scenario='damage; three ordered offset/xor-mask pairs per case', seed=182, count=100, unit='case'),
}

SOURCE_SHA256 = {'scripts/check_zip.py': '35bc20cf3d9db35f572a51891d65a306c69859c89b0189790c56b97f5a480794', 'scripts/check_module_container.py': '3e18077a3e3eabb8ef40b6841366af758b7ab1b1f55c1455ce8edab448bc6e4f', 'scripts/check_module_writer.py': '087b8f3f5cbe8a5534d92d91bc08255711a01dfb84b0d058e9b0344478f5da7e'}
