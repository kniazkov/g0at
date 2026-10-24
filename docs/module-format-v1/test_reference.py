"""Independent conformance assertions for the specification artifacts."""
import copy
import hashlib
import io
import json
import struct
import unittest
import zipfile
from pathlib import Path

import reference as r

ROOT = Path(__file__).parent


class FormatTests(unittest.TestCase):
    def test_schema_snapshots(self):
        from schema import ROOTS, schema
        for kind in ROOTS:
            self.assertEqual(json.loads((ROOT/(kind+'.schema.json')).read_text()), schema(kind))
        self.assertEqual(json.loads((ROOT/'opcodes.json').read_text()),
                         {name:i for i,name in enumerate(r.OPCODES)})

    def module(self):
        return r.parse((ROOT/'vectors/empty.module.json').read_bytes())

    def reject(self, value, kind='module'):
        with self.assertRaises((ValueError, UnicodeError)):
            r.normalize(value, kind)

    def test_strict_json(self):
        for text in ['{"a":1,"\\u0061":2}', '{"n":1e0}', '{"n":1.0}',
                     '{"n":NaN}', '\ufeff{}', '{"s":"\\ud800"}', '{"s":"\\udc00"}']:
            with self.subTest(text=text), self.assertRaises((ValueError, UnicodeError)):
                r.parse(text)
        self.assertEqual(r.parse('"\\ud83d\\ude00"'), '😀')
        self.assertEqual(r.parse('9223372036854775807'), 9223372036854775807)

    def test_canonical_bytes(self):
        self.assertEqual(r.canonical({'z':'a\n\0/"\\', 'a':-0}),
                         b'{"a":0,"z":"a\\u000a\\u0000/\\"\\\\"}')
        self.assertNotEqual(r.canonical('é'), r.canonical('e\u0301'))
        self.assertEqual(r.canonical(r.parse('"\\ud83d\\ude00"')), '"😀"'.encode())
        self.assertEqual(r.canonical({'\U00010000':1,'\ue000':2}), '{"\ue000":2,"\U00010000":1}'.encode())

    def test_shapes(self):
        m = self.module()
        self.assertEqual(r.normalize(m), m)
        for field in ['formatVersion', 'bytecodeVersion','name','version','mainHash','main','files']:
            v = copy.deepcopy(m)
            del v[field]
            self.reject(v)
        for value in [None, [], '1', True, 2]:
            v = copy.deepcopy(m)
            v['formatVersion'] = value
            self.reject(v)
        v=copy.deepcopy(m); v['surprise']=1; self.reject(v)
        v=copy.deepcopy(m); v['files'][0]['id']=0; self.reject(v)
        v=copy.deepcopy(m); v['name']='con.txt'; self.reject(v)
        v=copy.deepcopy(m); v['main']=1; self.reject(v)

    def test_gmod(self):
        g = r.parse((ROOT/'vectors/empty.gmod').read_bytes())
        self.assertEqual(r.normalize(g,'gmod')['native'], {'mode':'auto','target':'host'})
        for item in [None, {'mode':'atuo'}, {'target':3}, {'surprise':1}]:
            v=copy.deepcopy(g); v['native']=item; self.reject(v,'gmod')
        v=copy.deepcopy(g); v['main']='a\0b'; self.reject(v,'gmod')

    def test_constants(self):
        for value in ['A\0B', '', '😀']:
            self.assertEqual(r.facts({'type':'string','value':value})['value'],value)
        for value in ['\0', 'Я', '😀']:
            self.assertEqual(r.facts({'type':'character','value':value})['value'],value)
        for value in ['', 'ab', 'e\u0301', '\ud800']:
            with self.assertRaises((ValueError, UnicodeError)):
                r.facts({'type':'character','value':value})
        for bits in ['0000000000000000','8000000000000000','7ff8000000000001','7ff8000000000002']:
            self.assertEqual(r.facts({'type':'real','value':bits})['value'],bits)
        self.assertEqual(r.facts({'type':'null','value':None}),{'type':'null'})
        for facts in [{'type':'integer','value':1<<63}, {'type':'integer','value':True},
                      {'type':'real','value':1}, {'type':'boolean','value':0}, {'type':'null','nonNull':True}]:
            with self.assertRaises(ValueError): r.facts(facts)

    def test_facts_normalization(self):
        cases = [({'type':'integer','range':{'min':42,'max':42}}, {'type':'integer','value':42}),
                 ({'type':'integer','range':{'min':-(1<<63),'max':(1<<63)-1}}, {'type':'integer'}),
                 ({'type':'array','empty':True,'elements':{'type':'integer'}}, {'type':'array','empty':True}),
                 ({'type':'array','elements':{}}, {'type':'array'}),
                 ({'type':'integer','nonNull':True}, {'type':'integer'})]
        for inp, expected in cases:
            self.assertEqual(r.facts(inp), expected)
            self.assertEqual(r.facts(expected), expected)
        with self.assertRaises(ValueError):
            r.facts({'type':'array','empty':True,'elements':{'type':'integer','range':{'min':2,'max':1}}})

    def test_summaries(self):
        s={'when':{'arguments':[{},{}]},'result':{},'effects':{'may':[],'unknown':True}}
        a={'formatVersion':1,'summaries':[s]}
        self.assertIsNone(r.normalize_analysis(a,2))
        a['summaries'].append({'when':{}})
        with self.assertRaises(ValueError): r.normalize_analysis(a,2)
        with self.assertRaises(ValueError): r.normalize_analysis({'formatVersion':1,'summaries':[s]},1)
        a={'formatVersion':1,'summaries':[{'when':{},'effects':{'may':[],'unknown':False}}]}
        self.assertEqual(r.normalize_analysis(a,0), a)
        with self.assertRaises(ValueError):
            r.normalize_analysis({'formatVersion':1,'summaries':[{'when':{},'normalReturn':False,'result':{}}]},0)

    def test_dependency_versions(self):
        v=r.parse((ROOT/'vectors/two-versions.module.json').read_bytes())
        normalized=r.normalize(v)
        v['dependencies'].reverse()
        self.assertEqual(r.normalize(v), normalized)
        v['dependencies'].append(copy.deepcopy(v['dependencies'][0]))
        self.reject(v)

    def test_cross_part_validation(self):
        b=bytes.fromhex((ROOT/'vectors/empty.bytecode.hex').read_text())
        r.check_module(self.module(), b)
        invalid=r.parse((ROOT/'vectors/invalid-entry.module.json').read_bytes())
        with self.assertRaises(ValueError): r.check_module(invalid,b)
        imports=bytes.fromhex((ROOT/'vectors/two-versions.bytecode.hex').read_text())
        m=r.parse((ROOT/'vectors/two-versions.module.json').read_bytes())
        r.check_module(m, imports)
        with self.assertRaises(ValueError): r.check_module(m,b)
        optimized = copy.deepcopy(m)
        optimized['mainHash'] = r.main_hash(optimized, b)
        r.check_module(optimized, b)  # Syntactic dependencies survive eliminated IMPORTs.
        undeclared = copy.deepcopy(m)
        undeclared['dependencies'] = undeclared['dependencies'][:1]
        undeclared['mainHash'] = r.main_hash(undeclared, imports)
        with self.assertRaises(ValueError): r.check_module(undeclared, imports)
        for version in ['1','2']:
            dependency=r.parse((ROOT/('vectors/math-'+version+'.module.json')).read_bytes())
            r.check_module(dependency,b)
            self.assertIn(dependency['mainHash'],[d['hash'] for d in m['dependencies']])

    def test_hash_projection(self):
        m=self.module(); b=r.minimal_bytecode()
        self.assertEqual(r.main_hash(m,b),m['mainHash'])
        changed=copy.deepcopy(m)
        changed.update(description='Other',mainHash='f'*64,producers=[{'name':'cc','version':'1','buildId':'test'}])
        self.assertEqual(r.main_hash(changed,b),m['mainHash'])
        changed=copy.deepcopy(m); changed['name']='other'
        self.assertNotEqual(r.main_hash(changed,b),m['mainHash'])
        changed=copy.deepcopy(m); changed['externals']={'x':{'mutable':False}}
        self.assertNotEqual(r.main_hash(changed,b),m['mainHash'])
        expected=r.parse((ROOT/'vectors/hashes.json').read_bytes())
        preimage=bytes.fromhex(expected['hashInputHex'])
        self.assertEqual(preimage[:18],b'GOAT-MODULE-MAIN\0\1')
        self.assertEqual(hashlib.sha256(preimage).hexdigest(),m['mainHash'])

    def test_bytecode_layout(self):
        raw=bytes.fromhex((ROOT/'vectors/empty.bytecode.hex').read_text())
        self.assertEqual(len(raw),112)
        self.assertEqual(raw[:8],b'GOATBC01')
        self.assertEqual(struct.unpack_from('<6I',raw,40),(64,72,72,88,88,112))
        self.assertEqual(raw[64:72],b'\x02\0\0\0\0\0\0\0')
        before=bytes(raw); r.check_bytecode(raw); self.assertEqual(raw,before)
        for offset,val in [(0,0),(8,2),(16,0),(40,63),(65,1),(64,255)]:
            bad=bytearray(raw); bad[offset]=val
            with self.assertRaises(ValueError): r.check_bytecode(bad)
        with self.assertRaises(ValueError): r.check_bytecode(raw[:-1])
        imported=bytes.fromhex((ROOT/'vectors/two-versions.bytecode.hex').read_text())
        r.check_bytecode(imported)

    def test_archive_with_independent_reader(self):
        raw=bytes.fromhex((ROOT/'vectors/empty.gbin.hex').read_text())
        self.assertEqual(raw,(ROOT/'vectors/empty.gbin').read_bytes())
        self.assertEqual((ROOT/'vectors/empty.bytecode.bin').read_bytes(),r.minimal_bytecode())
        with zipfile.ZipFile(io.BytesIO(raw)) as z:
            self.assertEqual(z.namelist(),['module.json','bytecode.bin'])
            self.assertIsNone(z.testzip())
            for item in z.infolist():
                self.assertEqual(item.compress_type,8)
                self.assertEqual(item.date_time,(1980,1,1,0,0,0))
                self.assertEqual(item.external_attr,0o100644<<16)
            m=r.parse(z.read('module.json'))
            self.assertEqual(r.main_hash(m,z.read('bytecode.bin')),m['mainHash'])
        expected=r.parse((ROOT/'vectors/hashes.json').read_bytes())
        self.assertEqual(hashlib.sha256(raw).hexdigest(),expected['archiveSha256'])

    def test_native_envelope(self):
        raw=bytes.fromhex((ROOT/'vectors/native-envelope.hex').read_text())
        self.assertEqual(raw[:8],b'GOATND01')
        length=struct.unpack_from('<Q',raw,8)[0]
        self.assertEqual(len(raw),17+length)
        self.assertEqual(raw[-1],0)
        n=r.parse(raw[16:-1]); self.assertEqual(r.normalize(n,'native'),n)
        n['specializations'][0]['when']['conditions'][0]['argument']=2
        self.reject(n,'native')

    def test_target(self):
        t=r.parse((ROOT/'vectors/target.json').read_bytes())
        self.assertEqual(r.normalize(t,'target'),t)
        t['endian']='big'; self.reject(t,'target')

    def test_guard_intersections(self):
        def guard(op, value):
            return {'argumentTypes':['integer'],'conditions':[{'argument':0,'operation':op,
                    'constant':{'type':'integer','value':value}}]}
        self.assertTrue(r.guards_overlap(guard('greater',0),guard('equal',2)))
        self.assertFalse(r.guards_overlap(guard('less',0),guard('greaterEqual',0)))
        first=r.parse((ROOT/'vectors/native-description.json').read_bytes())
        second=copy.deepcopy(first['specializations'][0])
        second.update(specializationId=2,symbol='goat_fn_2_v1')
        first['specializations'].append(second)
        self.reject(first,'native')

    def test_normalization_preserves_distinctions(self):
        m=self.module()
        m.update(authors=[{'name':'A','email':''}], functions=[], externals={}, native=[], description='')
        n=r.normalize(m)
        self.assertEqual(n['authors'],[{'name':'A'}])
        self.assertEqual(r.normalize(n),n)
        m['authors'].append({'name':'A'})
        self.reject(m)
        for effect in [{'may':[],'unknown':False},{'may':['output'],'unknown':True}]:
            a={'formatVersion':1,'summaries':[{'when':{},'effects':effect}]}
            self.assertEqual(r.normalize_analysis(a,0),a)
        partial={'formatVersion':1,'summaries':[{'when':{'arguments':[{}, {'type':'integer'}]},'result':{'type':'integer'}}]}
        self.assertEqual(r.normalize_analysis(partial,2),partial)

    def test_hash_excludes_native_but_validates_it(self):
        m=self.module(); b=r.minimal_bytecode()
        m['functions']=[{'functionId':1,'file':0,'instruction':0,'parameters':[{'name':'a'},{'name':'b'}]}]
        # This is a hash-projection input, not a bytecode-consistent module fixture.
        h=r.main_hash(m,b)
        n=r.parse((ROOT/'vectors/native-description.json').read_bytes())
        m['native']=[{'library':'native/variant-1.so','hash':'a'*64,'goatAbiVersion':1,
                      'target':n['target'],'specializations':n['specializations']}]
        self.assertEqual(r.main_hash(m,b),h)
        m['native'][0]['hash']='b'*64
        self.assertEqual(r.main_hash(m,b),h)
        m['native'][0]['hash']='bad'
        with self.assertRaises(ValueError): r.main_hash(m,b)


if __name__ == '__main__':
    unittest.main()
