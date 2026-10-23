"""Source of the closed v1 JSON schemas. No third-party Python dependency."""
import json


def obj(properties, required=()):
    return {"type": "object", "properties": properties,
            "required": list(required), "additionalProperties": False}


def arr(items, **kw):
    return {"type": "array", "items": items, **kw}


def ref(name):
    return {"$ref": "#/$defs/" + name}


U32 = {"type": "integer", "minimum": 0, "maximum": 4294967295}
ID = {**U32, "minimum": 1}
I64 = {"type": "integer", "minimum": -(1 << 63), "maximum": (1 << 63)-1}
TEXT = {"type": "string"}
NONEMPTY = {"type": "string", "minLength": 1}
BOOL = {"type": "boolean"}
ONE = {"const": 1}
TRUE = {"const": True}
NAME = {"type": "string", "pattern": r"^[a-z0-9](?:[a-z0-9._-]*[a-z0-9_-])?$", "maxLength": 255}
VERSION = {**NAME, "maxLength": 250}
HASH = {"type": "string", "pattern": "^[0-9a-f]{64}$"}
TYPES = ["null", "boolean", "integer", "real", "numeric", "character", "string", "function", "array", "object"]
SCALARS = ["null", "boolean", "integer", "real", "character", "string"]
INFO = {"description": TEXT, "license": TEXT, "homepage": TEXT,
        "authors": arr(ref("author")), "producers": arr(ref("producer"))}

DEFS = {
    "author": obj({"name": NONEMPTY, "email": TEXT}, ["name"]),
    "producer": obj({k: NONEMPTY for k in ["name", "version", "buildId"]}, ["name", "version", "buildId"]),
    "compiler": obj({"name": NONEMPTY, "version": NONEMPTY}, ["name", "version"]),
    "dependency": obj({"name": NAME, "version": VERSION, "hash": HASH}, ["name", "version", "hash"]),
    "file": obj({"name": NONEMPTY, "entry": U32, "external": BOOL}, ["name", "entry", "external"]),
    "parameter": obj({"name": NONEMPTY}, ["name"]),
    "functionRef": obj({"module": NAME, "version": VERSION, "functionId": ID},
                       ["module", "version", "functionId"]),
    "facts": obj({"type": {"enum": TYPES}, "nonNull": TRUE,
                  "value": {"type": ["null", "boolean", "integer", "string"]},
                  "range": obj({"min": I64, "max": I64}, ["min", "max"]),
                  "elements": ref("facts"), "empty": TRUE}),
    "when": obj({"arguments": arr(ref("facts"))}),
    "effects": obj({"may": arr({"enum": ["input", "output", "externalRead", "externalWrite"]}, uniqueItems=True),
                    "unknown": BOOL}, ["may", "unknown"]),
    "summary": obj({"when": ref("when"), "result": ref("facts"), "effects": ref("effects"),
                    "normalReturn": {"const": False}}, ["when"]),
    "analysis": obj({"formatVersion": ONE, "summaries": arr(ref("summary"))}, ["formatVersion", "summaries"]),
    "function": obj({"functionId": ID, "file": U32, "instruction": U32,
                     "parameters": arr(ref("parameter")), "analysis": ref("analysis")},
                    ["functionId", "file", "instruction", "parameters"]),
    "external": obj({"mutable": BOOL, "type": {"enum": TYPES},
                     "value": {"type": ["null", "boolean", "integer", "string"]},
                     "functionId": ID, "functionRef": ref("functionRef")}, ["mutable"]),
    "versionTuple": arr(U32, minItems=1, maxItems=4),
    "target": obj({"os": {"enum": ["linux", "windows"]},
                   "minimumOs": ref("versionTuple"),
                   "architecture": {"enum": ["x86", "x86_64", "aarch64"]},
                   "bits": {"enum": [32, 64]}, "endian": {"const": "little"},
                   "cpuFeatures": arr(NONEMPTY, uniqueItems=True),
                   "runtime": obj({"family": {"enum": ["glibc", "musl", "msvcrt", "ucrt"]},
                                   "minimumVersion": ref("versionTuple")}, ["family", "minimumVersion"])},
                  ["os", "minimumOs", "architecture", "bits", "endian", "cpuFeatures", "runtime"]),
    "constant": obj({"type": {"enum": SCALARS}, "value": {"type": ["null", "boolean", "integer", "string"]}}, ["type", "value"]),
    "predicate": obj({"argument": U32, "operation": {"enum": ["equal", "notEqual", "less", "lessEqual", "greater", "greaterEqual"]},
                      "constant": ref("constant")}, ["argument", "operation", "constant"]),
    "guard": obj({"argumentTypes": arr({"enum": SCALARS}), "conditions": arr(ref("predicate"))}, ["argumentTypes"]),
    "signature": obj({"id": {"const": "goat.adapter.v1"}, "arguments": arr({"enum": SCALARS}),
                      "result": {"enum": SCALARS}}, ["id", "arguments", "result"]),
    "specialization": obj({"specializationId": ID, "functionId": ID,
                           "symbol": {"type": "string", "pattern": "^goat_fn_[1-9][0-9]*_v1$"},
                           "signature": ref("signature"), "when": ref("guard")},
                          ["specializationId", "functionId", "symbol", "signature", "when"]),
    "variant": obj({"library": {"type": "string", "pattern": "^native/[a-z0-9][a-z0-9._-]*\\.(dll|so)$"},
                    "hash": HASH, "goatAbiVersion": ID, "target": ref("target"),
                    "specializations": arr(ref("specialization"), minItems=1), "cCompiler": ref("compiler")},
                   ["library", "hash", "goatAbiVersion", "target", "specializations"]),
}

ROOTS = {
    "gmod": obj({"formatVersion": ONE, "name": NAME, "version": VERSION, "main": NONEMPTY,
                 "dependencies": {"type": "object", "propertyNames": NAME, "additionalProperties": VERSION},
                 "native": obj({"mode": {"enum": ["off", "auto", "required"]},
                                "target": {"type": "string", "pattern": "^(host|[a-z0-9][a-z0-9_-]*(\\+[a-z0-9_-]+)*)$"}}),
                 **{k: v for k, v in INFO.items() if k != "producers"}}, ["formatVersion", "name", "version", "main"]),
    "module": obj({"formatVersion": ONE, "bytecodeVersion": ONE, "name": NAME, "version": VERSION,
                   "mainHash": HASH, "main": U32, "files": arr(ref("file"), minItems=1),
                   "dependencies": arr(ref("dependency")), "functions": arr(ref("function")),
                   "externals": {"type": "object", "propertyNames": NONEMPTY, "additionalProperties": ref("external")},
                   "native": arr(ref("variant")), **INFO},
                  ["formatVersion", "bytecodeVersion", "name", "version", "mainHash", "main", "files"]),
    "native": obj({"formatVersion": ONE, "module": NAME, "version": VERSION, "mainHash": HASH,
                   "goatAbiVersion": ID, "target": ref("target"), "specializations": arr(ref("specialization"), minItems=1)},
                  ["formatVersion", "module", "version", "mainHash", "goatAbiVersion", "target", "specializations"]),
    "target": ref("target"),
}


def schema(kind):
    return {"$schema": "https://json-schema.org/draft/2020-12/schema",
            "$id": "https://github.com/kniazkov/g0at/docs/module-format-v1/" + kind + ".schema.json",
            **ROOTS[kind], "$defs": DEFS}


if __name__ == "__main__":
    import sys
    print(json.dumps(schema(sys.argv[1]), ensure_ascii=False, indent=2))
