"""Transitional Python API for schema-source.json; no third-party dependency.

reference.py consumes DEFS/ROOTS/schema; generate_module_json_schema.py also
consumes NAME/HASH. The declarative source preserves insertion order and exact
integer tokens for the C generator migration (#332).
"""
import json
from pathlib import Path


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate schema source key: " + key)
        result[key] = value
    return result


def _reject_number(token):
    raise ValueError("Schema source requires integer tokens: " + token)


def _load_source(path):
    with path.open(encoding="utf-8") as stream:
        source = json.load(stream, object_pairs_hook=_unique_object,
                           parse_float=_reject_number, parse_constant=_reject_number)
    if not isinstance(source, dict) or set(source) != {"DEFS", "ROOTS"} or not all(
            isinstance(source[key], dict) for key in ("DEFS", "ROOTS")):
        raise ValueError("Schema source must contain DEFS and ROOTS objects")
    return source


_SOURCE = _load_source(Path(__file__).with_name("schema-source.json"))
DEFS = _SOURCE["DEFS"]
ROOTS = _SOURCE["ROOTS"]
# Compatibility aliases for the transitional C-table generator; not new sources.
NAME = ROOTS["gmod"]["properties"]["name"]
HASH = ROOTS["module"]["properties"]["mainHash"]


def schema(kind):
    return {"$schema": "https://json-schema.org/draft/2020-12/schema",
            "$id": "https://github.com/kniazkov/g0at/docs/module-format-v1/" + kind + ".schema.json",
            **ROOTS[kind], "$defs": DEFS}


if __name__ == "__main__":
    import sys
    print(json.dumps(schema(sys.argv[1]), ensure_ascii=False, indent=2))
