# Copyright 2026 Ivan Kniazkov
"""Shared strict schema checks for both metadata and real-process inspection tests."""
import json
import re


def schema_check(item, spec, document, directory, path='$'):
    """Validate the published schema with the stdlib, including external refs."""
    if '$ref' in spec:
        filename, _, fragment = spec['$ref'].partition('#')
        if filename:
            document = json.loads((directory / filename).read_text(encoding='utf-8'))
        target = document
        for part in fragment.split('/')[1:]:
            target = target[part.replace('~1', '/').replace('~0', '~')]
        schema_check(item, target, document, directory, path)
    for requirement in spec.get('allOf', []):
        schema_check(item, requirement, document, directory, path)
    if 'if' in spec:
        try:
            schema_check(item, spec['if'], document, directory, path)
            matched = True
        except AssertionError:
            matched = False
        schema_check(item, spec.get('then' if matched else 'else', {}), document, directory, path)
    if 'anyOf' in spec:
        errors = []
        for choice in spec['anyOf']:
            try:
                schema_check(item, choice, document, directory, path)
                break
            except AssertionError as error:
                errors.append(str(error))
        else:
            raise AssertionError((path, 'anyOf', errors))
    if 'oneOf' in spec:
        matches = 0
        for choice in spec['oneOf']:
            try:
                schema_check(item, choice, document, directory, path)
                matches += 1
            except AssertionError:
                pass
        assert matches == 1, (path, 'oneOf', matches)
    kinds = {'null': lambda v: v is None, 'boolean': lambda v: type(v) is bool,
             'integer': lambda v: type(v) is int,
             'number': lambda v: type(v) in (int, float),
             'string': lambda v: isinstance(v, str),
             'array': lambda v: isinstance(v, list),
             'object': lambda v: isinstance(v, dict)}
    if 'type' in spec:
        types = spec['type'] if isinstance(spec['type'], list) else [spec['type']]
        assert any(kinds[k](item) for k in types), (path, types, item)
    if 'const' in spec:
        assert type(item) is type(spec['const']) and item == spec['const'], (path, item)
    if 'enum' in spec:
        assert any(type(item) is type(v) and item == v for v in spec['enum']), (path, item)
    if isinstance(item, dict):
        assert set(spec.get('required', [])) <= item.keys(), (path, 'required', item)
        properties = spec.get('properties', {})
        extra = spec.get('additionalProperties', True)
        for key, value in item.items():
            if key in properties:
                schema_check(value, properties[key], document, directory, path + '.' + key)
            elif extra is False:
                raise AssertionError((path, 'unknown property', key))
            elif isinstance(extra, dict):
                schema_check(value, extra, document, directory, path + '.' + key)
            if 'propertyNames' in spec:
                schema_check(key, spec['propertyNames'], document, directory, path)
    if isinstance(item, list):
        assert len(item) >= spec.get('minItems', 0), (path, item)
        assert len(item) <= spec.get('maxItems', len(item)), (path, item)
        if spec.get('uniqueItems'):
            assert len({json.dumps(x, sort_keys=True) for x in item}) == len(item), (path, item)
        for index, value in enumerate(item):
            schema_check(value, spec.get('items', {}), document, directory, f'{path}[{index}]')
    if isinstance(item, str):
        assert len(item) >= spec.get('minLength', 0), (path, item)
        assert len(item) <= spec.get('maxLength', len(item)), (path, item)
        if 'pattern' in spec:
            assert re.search(spec['pattern'], item), (path, item, spec['pattern'])
    if type(item) in (int, float):
        assert item >= spec.get('minimum', item), (path, item)
        assert item <= spec.get('maximum', item), (path, item)
