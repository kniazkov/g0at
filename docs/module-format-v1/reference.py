"""Executable format vectors, not the production module loader.

Strict parsing, schema shape validation, canonicalization and golden construction
use only Python's standard library. Graph resolution and execution are separate.
"""
import copy
import hashlib
import json
import math
import operator
import re
import struct
import zlib
from pathlib import Path

from schema import DEFS, ROOTS

MARKER = b"GOAT-MODULE-MAIN\x00\x01"
MAGIC = b"GOATBC01"
NATIVE_MAGIC = b"GOATND01"
HASH_FIELDS = ("formatVersion", "bytecodeVersion", "name", "version", "main",
               "files", "dependencies", "functions", "externals")
EFFECTS = ["externalRead", "externalWrite", "input", "output"]
OPCODES = ("NOP ARG END JUMP JIF POP DUP NIL TRUE FALSE ILOAD32 ILOAD64 RLOAD SLOAD CLOAD "
           "VLOAD VAR CONST STORE GETPROP SETPROP UPLUS UMINUS INC DEC LNOT BOOL BNOT LAND LOR "
           "BAND BOR BXOR SHL SHR ADD SUB MUL DIVIDE MODULO POWER LESS LEQ GREATER GREQ EQUAL "
           "DIFF FUNC CALL CALLM RET ENTER LEAVE RESTORE TRY THROW GETINDEX SETINDEX ARRAY "
           "INHERIT LOOP_ENTER LOOP_JUMP DUPN ROTATE SNAPSHOT ITER_NEXT FINALLY_ENTER "
           "FINALLY_LEAVE FINALLY_END DVAR DCONST BLOAD BSTORE INCLUDE IMPORT MAIN DIR CHECKPROP CHECKINDEX CHECKVAR CHECKBOUND").split()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def parse(data):
    if isinstance(data, bytes):
        data = data.decode("utf-8", "strict")
    require(not data.startswith("\ufeff"), "JSON BOM")

    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate JSON key: " + key)
            result[key] = value
        return result

    def bad_number(value):
        raise ValueError("non-integer number spelling: " + value)

    value = json.loads(data, object_pairs_hook=pairs, parse_float=bad_number,
                       parse_constant=bad_number)

    def unicode_check(v):
        if isinstance(v, str):
            v.encode("utf-8", "strict")
        elif isinstance(v, dict):
            for k, item in v.items():
                unicode_check(k)
                unicode_check(item)
        elif isinstance(v, list):
            for item in v:
                unicode_check(item)
    unicode_check(value)
    return value


def canonical(value):
    def string(s):
        s.encode("utf-8", "strict")
        return '"' + ''.join('\\u%04x' % ord(c) if ord(c) < 32 else
                             '\\' + c if c in '\\"' else c for c in s) + '"'

    def encode(v):
        if v is None:
            return "null"
        if type(v) is bool:
            return "true" if v else "false"
        if type(v) is int:
            return str(v)
        if isinstance(v, str):
            return string(v)
        if isinstance(v, list):
            return '[' + ','.join(map(encode, v)) + ']'
        require(isinstance(v, dict), "unsupported canonical JSON type")
        return '{' + ','.join(string(k) + ':' + encode(v[k]) for k in
                              sorted(v, key=lambda k: k.encode('utf-8'))) + '}'
    return encode(value).encode("utf-8")


def shape(value, schema, path="$", defs=DEFS):
    """Validator for the explicitly used subset of JSON Schema 2020-12."""
    if "$ref" in schema:
        return shape(value, defs[schema["$ref"].split('/')[-1]], path, defs)
    if "const" in schema:
        require(type(value) is type(schema["const"]) and value == schema["const"], path + ": const")
    if "enum" in schema:
        require(any(type(value) is type(v) and value == v for v in schema["enum"]), path + ": enum")
    types = schema.get("type", [])
    if isinstance(types, str):
        types = [types]
    matches = {"null": value is None, "boolean": type(value) is bool,
               "integer": type(value) is int, "string": isinstance(value, str),
               "object": isinstance(value, dict), "array": isinstance(value, list)}
    require(not types or any(matches[t] for t in types), path + ": type")
    if isinstance(value, dict):
        props = schema.get("properties", {})
        require(set(schema.get("required", [])) <= value.keys(), path + ": missing field")
        extra = schema.get("additionalProperties", True)
        for key, v in value.items():
            if "propertyNames" in schema:
                shape(key, schema["propertyNames"], path + ".key", defs)
            if key in props:
                shape(v, props[key], path + "." + key, defs)
            else:
                require(extra is not False, path + ": unknown field " + key)
                if isinstance(extra, dict):
                    shape(v, extra, path + "." + key, defs)
    elif isinstance(value, list):
        require(len(value) >= schema.get("minItems", 0), path + ": short array")
        require(len(value) <= schema.get("maxItems", len(value)), path + ": long array")
        if schema.get("uniqueItems"):
            require(len({canonical(v) for v in value}) == len(value), path + ": duplicate item")
        for v in value:
            shape(v, schema.get("items", {}), path + "[]", defs)
    elif isinstance(value, str):
        value.encode("utf-8", "strict")
        require(len(value) >= schema.get("minLength", 0), path + ": empty string")
        require(len(value) <= schema.get("maxLength", len(value)), path + ": long string")
        if "pattern" in schema:
            require(re.fullmatch(schema["pattern"], value) is not None, path + ": pattern")
    elif type(value) is int:
        require(value >= schema.get("minimum", value) and value <= schema.get("maximum", value), path + ": range")


def identifier(name):
    stem = name.split('.')[0]
    require(stem not in {"con", "prn", "aux", "nul"} and
            not re.fullmatch(r"(?:com|lpt)[0-9]", stem), "reserved identifier")


def scalar(kind, value):
    if kind == "integer":
        require(type(value) is int and -(1 << 63) <= value < (1 << 63), "integer constant")
    elif kind == "real":
        require(isinstance(value, str) and re.fullmatch('[0-9a-f]{16}', value), "real bits")
    elif kind == "character":
        require(isinstance(value, str) and len(value) == 1, "character scalar")
        value.encode("utf-8", "strict")
    elif kind == "string":
        require(isinstance(value, str), "string constant")
        value.encode("utf-8", "strict")
    elif kind == "boolean":
        require(type(value) is bool, "boolean constant")
    elif kind == "null":
        require(value is None, "null constant")
    else:
        raise ValueError("non-scalar constant")


def facts(value):
    shape(value, DEFS["facts"])
    v = copy.deepcopy(value)
    kind = v.get("type")
    require(not (kind == "null" and v.get("nonNull")), "contradictory nonNull")
    if "value" in v:
        scalar(kind, v["value"])
        if kind == "null":
            del v["value"]
    if "range" in v:
        require(kind == "integer", "range requires integer")
        lo, hi = v["range"]["min"], v["range"]["max"]
        require(lo <= hi, "reversed range")
        if "value" in v:
            require(lo <= v["value"] <= hi, "constant outside range")
            del v["range"]
        elif lo == hi:
            v["value"] = lo
            del v["range"]
        elif lo == -(1 << 63) and hi == (1 << 63)-1:
            del v["range"]
    if "elements" in v:
        require(kind == "array", "elements requires array")
        v["elements"] = facts(v["elements"])
        if not v["elements"] or v.get("empty"):
            del v["elements"]
    require("empty" not in v or kind == "array", "empty requires array")
    if kind and kind != "null":
        v.pop("nonNull", None)
    return v


def unique(records, key, message):
    keys = [key(v) for v in records]
    require(len(set(keys)) == len(keys), message)


def facts_intersect(a, b):
    """Existence of a value satisfying both validated, normalized descriptions."""
    def types(v):
        kind = v.get("type")
        kinds = ({"integer", "real"} if kind == "numeric" else {kind}) if kind else {
            "null", "boolean", "integer", "real", "character", "string",
            "function", "array", "object"}
        return kinds - {"null"} if v.get("nonNull") else kinds

    common = types(a) & types(b)
    if "integer" in common:
        def bounds(v):
            if "value" in v:
                return v["value"], v["value"]
            return v.get("range", {}).get("min", -(1 << 63)), v.get("range", {}).get("max", (1 << 63)-1)
        amin, amax = bounds(a)
        bmin, bmax = bounds(b)
        if amin <= bmax and bmin <= amax:
            return True
        common.remove("integer")
    if not common:
        return False
    # Any pair of array descriptions admits the empty array. Scalar constants
    # use exact wire values (including binary64 bits), not arithmetic equality.
    return "value" not in a or "value" not in b or a["value"] == b["value"]


def consistent_summaries(summaries, arity):
    for i, a in enumerate(summaries):
        if "result" not in a:
            continue
        for b in summaries[i + 1:]:
            if "result" not in b or facts_intersect(a["result"], b["result"]):
                continue
            aa = a["when"].get("arguments", [{}] * arity)
            ba = b["when"].get("arguments", [{}] * arity)
            require(not all(facts_intersect(x, y) for x, y in zip(aa, ba)),
                    "contradictory normal-result summaries")


def normalize_analysis(value, arity):
    shape(value, DEFS["analysis"])
    summaries = []
    for original in value["summaries"]:
        s = copy.deepcopy(original)
        if "arguments" in s["when"]:
            args = s["when"]["arguments"]
            require(len(args) == arity, "analysis arity")
            args = [facts(v) for v in args]
            s["when"] = {"arguments": args} if any(args) else {}
        require(not ("normalReturn" in s and "result" in s), "no normal return with result")
        if "result" in s:
            s["result"] = facts(s["result"])
            if not s["result"]:
                del s["result"]
        if "effects" in s:
            s["effects"]["may"].sort(key=EFFECTS.index)
            if not s["effects"]["may"] and s["effects"]["unknown"]:
                del s["effects"]
        summaries.append(s)
    unique(summaries, canonical, "duplicate normalized summary")
    consistent_summaries(summaries, arity)
    summaries = sorted((s for s in summaries if set(s) != {"when"}), key=canonical)
    return {"formatVersion": 1, "summaries": summaries} if summaries else None


def normalize(value, kind="module"):
    shape(value, ROOTS[kind])
    v = copy.deepcopy(value)
    for k in ("name", "version", "module"):
        if k in v:
            identifier(v[k])
    for k in ("description", "license", "homepage"):
        if v.get(k) == "":
            del v[k]
    for a in v.get("authors", []):
        if a.get("email") == "":
            del a["email"]
    unique(v.get("authors", []), lambda a: (a["name"], a.get("email", "")), "duplicate author")
    unique(v.get("producers", []), canonical, "duplicate producer")
    if "producers" in v:
        v["producers"].sort(key=lambda p: tuple(p[k].encode('utf-8') for k in ("name", "version", "buildId")))
    if v.get("authors") == []:
        del v["authors"]
    if kind == "gmod":
        require('\0' not in v['main'], "NUL path")
        for name, version in v.get("dependencies", {}).items():
            identifier(name)
            identifier(version)
        v.setdefault("dependencies", {})
        v.setdefault("native", {})
        v["native"].setdefault("mode", "auto")
        v["native"].setdefault("target", "host")
        return v
    if kind == "module":
        require(v["main"] < len(v["files"]), "main file index")
        for f in v["files"]:
            require('\0' not in f['name'], "NUL logical filename")
        unique([f for f in v["files"] if not f["external"]], lambda f: f["name"], "duplicate internal filename")
        deps = v.get("dependencies", [])
        for d in deps:
            identifier(d["name"])
            identifier(d["version"])
        unique(deps, lambda d: (d["name"], d["version"]), "duplicate dependency")
        deps.sort(key=lambda d: (d["name"].encode('utf-8'), d["version"].encode('utf-8')))
        functions = v.get("functions", [])
        unique(functions, lambda f: f["functionId"], "duplicate functionId")
        functions.sort(key=lambda f: f["functionId"])
        ids = {f["functionId"] for f in functions}
        for f in functions:
            require(f["file"] < len(v["files"]), "function file")
            if "analysis" in f:
                a = normalize_analysis(f["analysis"], len(f["parameters"]))
                if a:
                    f["analysis"] = a
                else:
                    del f["analysis"]
        for e in v.get("externals", {}).values():
            require(not (e["mutable"] and any(k in e for k in ("type", "value", "functionId", "functionRef"))), "mutable guarantee")
            require(not ("functionId" in e and "functionRef" in e), "two function bindings")
            if "value" in e:
                scalar(e.get("type"), e["value"])
                if e.get("type") == "null":
                    del e["value"]
            if "functionId" in e or "functionRef" in e:
                require(e.get("type", "function") == "function", "function binding type")
            if "functionId" in e:
                require(e["functionId"] in ids, "missing function")
            if "functionRef" in e:
                identifier(e["functionRef"]["module"])
                identifier(e["functionRef"]["version"])
        for native in v.get("native", []):
            target(native["target"])
            guards(native["specializations"], {f['functionId']: len(f['parameters']) for f in functions})
        unique(v.get("native", []), lambda n: n["library"], "duplicate native library")
        if "native" in v:
            v["native"].sort(key=lambda n: (canonical(n["target"]), n["goatAbiVersion"], n["hash"], n["library"].encode("utf-8")))
    elif kind == "native":
        target(v["target"])
        guards(v["specializations"])
    elif kind == "target":
        target(v)
    for k in ("authors", "producers", "dependencies", "functions", "externals", "native"):
        if k in v and not v[k]:
            del v[k]
    return v


def target(t):
    shape(t, DEFS["target"])
    require(t["bits"] == (32 if t["architecture"] == "x86" else 64), "target bitness")
    require(t["runtime"]["family"] in ({"glibc", "musl"} if t["os"] == "linux" else {"msvcrt", "ucrt"}), "runtime family")
    if t["os"] == "windows":
        require((t["minimumOs"] + [0]*4)[:4] >= [10, 0, 0, 0], "Windows minimum")
    t["cpuFeatures"].sort(key=lambda s: s.encode('utf-8'))
    for parts in (t["minimumOs"], t["runtime"]["minimumVersion"]):
        while len(parts) > 1 and parts[-1] == 0:
            parts.pop()


def guards(entries, arities=None):
    unique(entries, lambda e: e["specializationId"], "duplicate specializationId")
    unique(entries, lambda e: e["symbol"], "duplicate native symbol")
    for e in entries:
        g, sig = e["when"], e["signature"]
        require(g["argumentTypes"] == sig["arguments"], "guard/signature types")
        if arities is not None:
            require(e["functionId"] in arities and len(sig["arguments"]) == arities[e["functionId"]], "specialization arity")
        require(e["symbol"] == 'goat_fn_%d_v1' % e["specializationId"], "symbol identity")
        for p in g.get("conditions", []):
            i = p["argument"]
            require(i < len(sig["arguments"]), "guard argument")
            c = p["constant"]
            require(c["type"] == sig["arguments"][i], "guard constant type")
            scalar(c["type"], c["value"])
            require(p["operation"] in ("equal", "notEqual") or c["type"] in ("integer", "real", "character"), "ordered guard type")
        unique(g.get("conditions", []), canonical, "duplicate guard condition")
        if g.get("conditions"):
            g["conditions"].sort(key=canonical)
        else:
            g.pop("conditions", None)
    entries.sort(key=lambda e: e["specializationId"])
    for i, first in enumerate(entries):
        for second in entries[i+1:]:
            if first['functionId'] == second['functionId']:
                require(not guards_overlap(first['when'], second['when']), 'overlapping native guards')


def guards_overlap(a, b):
    """Find a witness in each scalar domain; guards have no cross-argument terms."""
    if a['argumentTypes'] != b['argumentTypes']:
        return False
    ops = {'equal':operator.eq,'notEqual':operator.ne,'less':operator.lt,
           'lessEqual':operator.le,'greater':operator.gt,'greaterEqual':operator.ge}
    for index, kind in enumerate(a['argumentTypes']):
        predicates = [p for g in (a,b) for p in g.get('conditions',[]) if p['argument'] == index]
        values = [p['constant']['value'] for p in predicates]
        if kind == 'real':
            values = [struct.unpack('>d', bytes.fromhex(v))[0] for v in values]
            candidates = [0.0, float('inf'), -float('inf'), float('nan')]
            for v in values:
                candidates += [v, math.nextafter(v, -math.inf), math.nextafter(v, math.inf)]
        elif kind == 'integer':
            candidates = [-(1<<63),(1<<63)-1,0] + [v+d for v in values for d in (-1,0,1)]
            candidates = [v for v in candidates if -(1<<63) <= v < (1<<63)]
        elif kind == 'character':
            codes = [0,0x10ffff,0xe000,0xd7ff] + [ord(v)+d for v in values for d in (-1,0,1)]
            candidates = [chr(v) for v in codes if 0 <= v <= 0x10ffff and not 0xd800 <= v <= 0xdfff]
        elif kind == 'string':
            candidate = ''
            while candidate in values:
                candidate += 'x'
            candidates = values + [candidate]
        elif kind == 'boolean':
            candidates = [False, True]
        else:
            candidates = [None]
        if not any(all(ops[p['operation']](v,c) for p,c in zip(predicates,values)) for v in candidates):
            return False
    return True


def main_bytes(metadata):
    v = normalize(metadata)
    return canonical({k: v[k] for k in HASH_FIELDS if k in v})


def main_hash(metadata, bytecode):
    m = main_bytes(metadata)
    return hashlib.sha256(MARKER + struct.pack('<Q', len(m)) + m +
                          struct.pack('<Q', len(bytecode)) + bytecode).hexdigest()


def minimal_bytecode():
    # Header 64, instructions 8, file records 16, debug runs 24 = 112 bytes.
    header = struct.pack('<8s14I', MAGIC, 1, 64, 112, 1, 0, 1, 0, 1,
                         64, 72, 72, 88, 88, 112)
    return (header + struct.pack('<BBHI', 2, 0, 0, 0) +
            struct.pack('<4I', 0, 1, 0, 0) + struct.pack('<6I', 0, 1, 0, 1, 1, 0))


def importing_bytecode():
    names = ["math@1.0", "math@2.0"]
    payload, descriptors = bytearray(), bytearray()
    for name in names:
        payload.extend(b'\0' * ((-len(payload)) % 8))
        raw = name.encode('utf-32-le') + b'\0'*4
        descriptors.extend(struct.pack('<4I', len(payload), len(raw), 1, len(name)))
        payload.extend(raw)
    # IMPORT/POP twice, END. One logical source position run.
    ni, nd = 5, 2
    od = 64 + ni*8
    of = od + nd*16
    ol = of + 16
    start = ol + 24
    header = struct.pack('<8s14I', MAGIC, 1, 64, start+len(payload), ni, nd, 1, 0, 1,
                         64, od, of, ol, ol, start)
    instructions = b''.join(struct.pack('<BBHI', op, 0, 0, arg) for op, arg in
                            [(74, 0), (5, 0), (74, 1), (5, 0), (2, 0)])
    return header + instructions + descriptors + struct.pack('<4I',0,5,0,0) + struct.pack('<6I',0,5,0,1,1,0) + payload


def check_bytecode(data):
    require(len(data) >= 64, "short bytecode")
    h = struct.unpack_from('<8s14I', data)
    magic, version, header, total, ni, nd, nf, nn, nl, oi, od, of, on, ol, payload = h
    require(magic == MAGIC and version == 1 and header == 64 and total == len(data), "bytecode header")
    require(ni > 0 and nf > 0, "empty code/files")
    require(oi == 64 and od == oi+8*ni and of == od+16*nd and on == of+16*nf and
            ol == on+32*nn and payload == ol+24*nl and payload <= total, "section layout")
    require(payload % 8 == 0, "payload alignment")
    for i in range(ni):
        op, flags, a, b = struct.unpack_from('<BBHI', data, oi+8*i)
        require(op <= 76 and flags == 0, "opcode/flags")
        if op in (2, 5, 75, 76):
            require(a == b == 0, "unused operands")
        if op in (3, 4, 28, 29, 54, 61, 65, 66, 67):
            require(b < ni, "jump range")
        if op in (11, 12, 47):
            require(i > 0 and data[oi+8*(i-1)] == 1, "missing ARG")
    previous_end = 0
    for i in range(nf):
        first, count, entry, reserved = struct.unpack_from('<4I', data, of+16*i)
        require(first == previous_end and count > 0 and first <= entry < first+count <= ni and reserved == 0, "file span")
        previous_end = first+count
    require(previous_end == ni, "file coverage")
    previous_end = 0
    for i in range(nl):
        first, count, file_id, line, column, function = struct.unpack_from('<6I', data, ol+24*i)
        require(first == previous_end and count > 0 and first+count <= ni and file_id < nf, "location range")
        require((line == 0) == (column == 0), "synthetic location")
        previous_end = first+count
    require(previous_end == ni, "location coverage")
    previous_end = 0
    for i in range(nd):
        offset, size, kind, count = struct.unpack_from('<4I', data, od+16*i)
        require(offset % 8 == 0 and offset <= total-payload and size <= total-payload-offset, "data bounds")
        raw = data[payload+offset:payload+offset+size]
        require(offset == (previous_end+7)//8*8 and not any(data[payload+previous_end:payload+offset]), "data layout/padding")
        require(kind in (1, 2, 3), "data kind")
        if kind == 1:
            require(size == 4*(count+1) and raw[-4:] == b'\0'*4, "string size/terminator")
            raw[:-4].decode('utf-32-le', 'strict')
        elif kind == 2:
            require(size == 4*count, "vector size")
        else:
            require(size == 16 and count == 1, "declaration size")
        previous_end = offset+size
    require(payload+previous_end == total, "unowned trailing bytes")
    return h


def check_module(metadata, bytecode):
    v = normalize(metadata)
    h = check_bytecode(bytecode)
    ni, nd, nf, nn = h[4:8]
    oi, od, of, on, ol, payload = h[9:15]
    require(len(v['files']) == nf and len(v.get('functions', [])) == nn, 'binary/JSON table counts')
    for i, f in enumerate(v['files']):
        require(struct.unpack_from('<I', bytecode, of+16*i+8)[0] == f['entry'], 'binary/JSON file entry')
    for i, f in enumerate(v.get('functions', [])):
        fid, file, ins, body, params, count, name, reserved = struct.unpack_from('<8I', bytecode, on+32*i)
        require((fid,file,ins,count) == (f['functionId'],f['file'],f['instruction'],len(f['parameters'])), 'binary/JSON function')
        require(ins < ni and bytecode[oi+8*ins] == 47 and body < ni and reserved == 0, 'function record')
    required_imports = set()
    for i in range(ni):
        op, flags, a, b = struct.unpack_from('<BBHI', bytecode, oi+8*i)
        if op == 73:
            require(b < nf, 'include file')
        if op == 74:
            require(b < nd, 'import descriptor')
            off, size, kind, count = struct.unpack_from('<4I', bytecode, od+16*b)
            require(kind == 1, 'import string')
            name = bytecode[payload+off:payload+off+size-4].decode('utf-32-le')
            require(name.count('@') == 1, 'qualified import')
            required_imports.add(tuple(name.split('@')))
    # Dependencies retain syntactic source imports even after optimization removes code.
    require(required_imports <= {(d['name'],d['version']) for d in v.get('dependencies',[])}, 'undeclared import')
    require(main_hash(v, bytecode) == v['mainHash'], 'main hash mismatch')
    return v


def zip_bytes(parts, compressed=True):
    """Exact standard writer envelope; zlib version is part of the profile."""
    require(not compressed or zlib.ZLIB_RUNTIME_VERSION == "1.3.2", "golden ZIP writer requires zlib 1.3.2")
    names = ['module.json', 'bytecode.bin'] + sorted(set(parts)-{'module.json', 'bytecode.bin'})
    out, directory = bytearray(), bytearray()
    for name in names:
        content = parts[name]
        encoded = name.encode('utf-8')
        method = 8 if compressed else 0
        if compressed:
            compressor = zlib.compressobj(9, zlib.DEFLATED, -15, 8, zlib.Z_DEFAULT_STRATEGY)
            packed = compressor.compress(content) + compressor.flush(zlib.Z_FINISH)
        else:
            packed = content
        crc, offset = zlib.crc32(content), len(out)
        out.extend(struct.pack('<I5H3I2H', 0x04034b50, 20, 0x800, method, 0, 33,
                               crc, len(packed), len(content), len(encoded), 0))
        out.extend(encoded + packed)
        directory.extend(struct.pack('<I6H3I5H2I', 0x02014b50, 0x0314, 20, 0x800, method, 0, 33,
                                     crc, len(packed), len(content), len(encoded), 0, 0, 0, 0,
                                     0o100644 << 16, offset))
        directory.extend(encoded)
    offset = len(out)
    out.extend(directory)
    out.extend(struct.pack('<I4H2IH', 0x06054b50, 0, 0, len(names), len(names), len(directory), offset, 0))
    return bytes(out)


def vectors():
    b = minimal_bytecode()
    m = {"formatVersion": 1, "bytecodeVersion": 1, "name": "empty", "version": "1.0",
         "mainHash": "0"*64, "main": 0, "files": [{"name": "main.goat", "entry": 0, "external": False}]}
    m['mainHash'] = main_hash(m, b)
    library1 = copy.deepcopy(m)
    library1.update(name="math", version="1.0")
    library1['mainHash'] = main_hash(library1, b)
    library2 = copy.deepcopy(library1)
    library2['version'] = '2.0'
    library2['mainHash'] = main_hash(library2, b)
    dep = copy.deepcopy(m)
    dep.update(name="two-versions", dependencies=[{"name": "math", "version": x['version'], "hash": x['mainHash']} for x in (library1, library2)])
    imports = importing_bytecode()
    dep['mainHash'] = main_hash(dep, imports)
    data = {"empty.bytecode.hex": b.hex()+'\n',
            "empty.module.json": canonical(m).decode(),
            "empty.main.json": main_bytes(m).decode(),
            "math-1.module.json": canonical(library1).decode()+'\n',
            "math-2.module.json": canonical(library2).decode()+'\n',
            "two-versions.module.json": canonical(dep).decode()+'\n',
            "two-versions.bytecode.hex": imports.hex()+'\n',
            "empty.gmod": '{"formatVersion":1,"name":"empty","version":"1.0","main":"main.goat"}\n',
            "unicode.json": '{"type":"string","value":"A\\u0000Я😀"}\n'}
    archive = zip_bytes({'module.json': canonical(m), 'bytecode.bin': b})
    data['empty.gbin.hex'] = archive.hex()+'\n'
    data['hashes.json'] = json.dumps({"mainHash": m['mainHash'], "bytecodeSha256": hashlib.sha256(b).hexdigest(),
                                     "archiveSha256": hashlib.sha256(archive).hexdigest(),
                                     "hashInputHex": (MARKER+struct.pack('<Q',len(main_bytes(m)))+main_bytes(m)+struct.pack('<Q',len(b))+b).hex()}, indent=2)+'\n'
    data['invalid-duplicate-key.json'] = '{"formatVersion":1,"formatVersion":1}\n'
    data['invalid-integer-spelling.json'] = '{"formatVersion":1e0}\n'
    data['invalid-surrogate.json'] = '{"description":"\\ud800"}\n'
    bad = copy.deepcopy(m)
    bad['files'][0]['entry'] = 1
    data['invalid-entry.module.json'] = canonical(bad).decode()+'\n'
    target_data = {"os":"linux", "minimumOs":[5,4], "architecture":"x86_64", "bits":64,
                   "endian":"little", "cpuFeatures":["sse2"],
                   "runtime":{"family":"glibc", "minimumVersion":[2,28]}}
    native = {"formatVersion":1,"module":"native-example","version":"1.0","mainHash":"1"*64,
              "goatAbiVersion":1,"target":target_data,"specializations":[{
                  "specializationId":1,"functionId":1,"symbol":"goat_fn_1_v1",
                  "signature":{"id":"goat.adapter.v1","arguments":["integer","integer"],"result":"integer"},
                  "when":{"argumentTypes":["integer","integer"],"conditions":[
                      {"argument":1,"operation":"notEqual","constant":{"type":"integer","value":0}}]}}]}
    n = canonical(normalize(native, 'native'))
    data['native-description.json'] = n.decode()+'\n'
    data['native-envelope.hex'] = (NATIVE_MAGIC+struct.pack('<Q',len(n))+n+b'\0').hex()+'\n'
    data['target.json'] = canonical(target_data).decode()+'\n'
    data['empty.gbin'] = archive
    data['empty.bytecode.bin'] = b
    data['two-versions.bytecode.bin'] = imports
    return data


def main():
    import argparse
    from schema import schema
    p = argparse.ArgumentParser()
    p.add_argument('--write', action='store_true', help='regenerate reviewed golden files')
    args = p.parse_args()
    root = Path(__file__).parent
    generated = {"vectors/"+k: v for k, v in vectors().items()}
    generated['opcodes.json'] = json.dumps({name:i for i,name in enumerate(OPCODES)}, indent=2)+'\n'
    generated.update({kind+'.schema.json': json.dumps(schema(kind), ensure_ascii=False, indent=2)+'\n' for kind in ROOTS})
    for path, content in generated.items():
        dest = root/path
        encoded = content if isinstance(content, bytes) else content.encode('utf-8')
        if args.write:
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(encoded)
        else:
            require(dest.read_bytes() == encoded, 'golden differs: '+path)
    print('Verified %d generated schema/vector files (zlib %s).' % (len(generated), zlib.ZLIB_RUNTIME_VERSION))


if __name__ == '__main__':
    main()
