# Independent JSON-format reference parity corpus (#335)

`cases.json` contains read-only inputs and expected answers captured from the
standard-library Python reference at parent commit
`7358333d0a49328d3612ff78040155e389718326`. The C oracle never computes an
expected answer with a production validator/canonicalizer. The source schemas
are loaded at runtime from `docs/module-format-v1/schema-source.json`; schema
rules are not duplicated as C tables.

## Scenario map

Each original method below is from `docs/module-format-v1/test_reference.py`.
Only top-level reference calls are captured: nested calls remain exercised by
the corresponding independent C operation. Rejections are recorded as such,
not discarded. Hash cases compare the **entire preimage** and its frozen SHA-256.

| Original method / supplemental group | Stable CTest ID | Cases |
|---|---|---:|
| `test_strict_json` | `format.oracle.strict_json` | 9 |
| `test_canonical_bytes` | `format.oracle.canonical_bytes` | 6 |
| `test_shapes` | `format.oracle.shapes` | 18 |
| `test_gmod` | `format.oracle.gmod` | 7 |
| `test_constants` | `format.oracle.constants` | 20 |
| `test_facts_normalization` | `format.oracle.facts_normalization` | 11 |
| `test_summaries` | `format.oracle.summaries` | 5 |
| `test_summary_consistency` | `format.oracle.summary_consistency` | 76 |
| `test_dependency_versions` | `format.oracle.dependency_versions` | 4 |
| `test_hash_projection` | `format.oracle.hash_projection` | 6 |
| `test_target` | `format.oracle.target` | 3 |
| `test_guard_intersections` | `format.oracle.guard_intersections` | 4 |
| `test_normalization_preserves_distinctions` | `format.oracle.normalization_preserves_distinctions` | 7 |
| `test_hash_excludes_native_but_validates_it` | `format.oracle.hash_excludes_native_but_validates_it` | 6 |
| `test_native_envelope` | `format.oracle.native_envelope` | 3 |
| edge_cases (supplement) | `format.oracle.edge_cases` | 23 |
| vectors (supplement) | `format.oracle.vectors` | 21 |
| metadata_edges (supplement) | `format.oracle.metadata_edges` | 43 |
| target_edges (supplement) | `format.oracle.target_edges` | 31 |
| guard_edges (supplement) | `format.oracle.guard_edges` | 225 |

Total: **528** cases. Corpus SHA-256:
`986091ddbed4968c29ff742b68a0bb43863390bcb10c83023783330a6953bcd0`.

`test_native_envelope` maps only its JSON parsing/normalization and guard-index
cases here; binary envelope assertions remain Python until #336. Likewise,
`test_cross_part_validation`, `test_bytecode_layout`, and
`test_archive_with_independent_reader` remain with #336. Schema snapshots and
schema-source checks retain their existing Python and C generator tests from
#331–#332; this change does not remove them.

Supplemental cases cover malformed UTF-8, arbitrary-precision integer spelling,
all control-character escapes, UTF-8 key ordering, frozen positive/negative JSON
vectors, Windows-reserved names, information/producers normalization, invalid
external bindings, target architecture/runtime/version constraints, and native
guard witnesses across int64 boundaries, binary64 NaN/infinities/signed zero,
Unicode character boundaries, embedded NUL strings, booleans and null. The
`invalid-entry.module.json` vector is valid at this JSON layer; its cross-part
bytecode rejection remains #336, not a silently weakened JSON test.

## Harness checks and independence

- `format.oracle.reject-corrupt-output` intentionally changes a canonical output
  byte; `WILL_FAIL` requires the parity assertion to detect it.
- `format.oracle.reject-accept-invalid` intentionally accepts invalid JSON;
  `WILL_FAIL` requires the validity assertion to detect it.
- Every invocation defines deliberately broken `goat_json_canonical` and
  `goat_module_json_read` symbols, calls both to prove those failures are active,
  then requires their call count to remain unchanged through the entire corpus.
  Thus an intentional production error cannot change any expected answer.
- No production module/validator library is linked. `yyjson` supplies the DOM and
  syntax reader only; schema checks, canonical ordering/escaping, normalization,
  guard decisions and hash preimage construction are independent test code.
- `format.oracle.ubsan` runs the full corpus with the oracle sources instrumented
  in the glibc GNU/Clang Linux CI builds; the portable tests run everywhere.
  The optional target is excluded from default builds: musl/MinGW toolchains
  may not provide the UBSan runtime.
- Hash projection and preimage operations also assert the input remains unchanged.

## Reproduce ordinary checks

From the repository root:

```sh
cmake -S . -B build/format-oracle -DCMAKE_BUILD_TYPE=Debug
cmake --build build/format-oracle --target format_oracle_testing --parallel 2
ctest --test-dir build/format-oracle -R '^format\.oracle\.' -E '\.ubsan$' --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 docs/module-format-v1/test_reference.py
```

On GNU/Clang Unix with the UBSan runtime available, also run:

```sh
cmake --build build/format-oracle --target format_oracle_ubsan_testing --parallel 2
ctest --test-dir build/format-oracle -R '^format\.oracle\.ubsan$' --output-on-failure
```

The ordinary checks only read the corpus. Temporary work is owned by the common
scenario framework below the selected build directory, and failures preserve
that directory. Every scenario has its own deadline and CTest ID.

## Explicit recapture (not part of ordinary tests)

The following one-time capture recipe is retained as documentation rather than
adding another runtime Python helper. Run it from the repository root **only
when intentionally updating frozen expectations**, and review the resulting
diff. It runs the original unittest assertions while intercepting their outer
reference calls. This is not a replacement random-number generator: all
supplemental choices below are explicit deterministic lists.

```python
import sys, json, copy, unittest, struct, hashlib
from pathlib import Path
root=Path.cwd()
sys.path.insert(0,str(root/'docs/module-format-v1'))
import reference as r, test_reference as t
names=['strict_json','canonical_bytes','shapes','gmod','constants','facts_normalization','summaries','summary_consistency','dependency_versions','hash_projection','target','guard_intersections','normalization_preserves_distinctions','hash_excludes_native_but_validates_it','native_envelope']
ops=['parse','canonical','normalize','facts','normalize_analysis','guards_overlap','main_bytes','main_hash']
original={n:getattr(r,n) for n in ops}
depth=0; group=''; records=[]
def dump(x): return json.dumps(x,ensure_ascii=True,separators=(',',':'))
def wrap(name):
 def wrapped(*args,**kwargs):
  global depth
  outer=depth==0
  if outer:
   row={'group':group,'operation':name}
   if name=='parse': row['inputHex']=(args[0] if isinstance(args[0],bytes) else args[0].encode('utf8')).hex()
   else: row['input']=dump(args[0])
   if name=='normalize': row['kind']=args[1] if len(args)>1 else kwargs.get('kind','module')
   if name=='normalize_analysis': row['arity']=args[1]
   if name=='guards_overlap': row['second']=dump(args[1])
   if name=='main_hash': row['bytecodeHex']=args[1].hex()
  depth+=1
  try:
   result=original[name](*args,**kwargs)
   if outer:
    row['valid']=True
    if name=='main_hash':
     m=original['main_bytes'](args[0]); b=args[1]
     row['expectedHex']=(r.MARKER+struct.pack('<Q',len(m))+m+struct.pack('<Q',len(b))+b).hex()
     row['sha256']=result
    elif isinstance(result,bytes): row['expectedHex']=result.hex()
    else: row['expected']=dump(result)
   return result
  except (ValueError,UnicodeError):
   if outer: row['valid']=False
   raise
  finally:
   depth-=1
   if outer: records.append(row)
 return wrapped
for n in ops: setattr(r,n,wrap(n))
for group in names:
 result=unittest.TestResult(); t.FormatTests('test_'+group).run(result)
 if not result.wasSuccessful(): raise RuntimeError(str(result.errors)+str(result.failures))
# Supplement strict parser boundaries, UTF-8 and canonical control spellings.
group='edge_cases'
for raw in [b'"\xc0\xaf"',b'"\xed\xa0\x80"',b'"\xf4\x90\x80\x80"',b'{"x":1,"x":2}',b'01',b'+1',b'1.',b'1e0',b'Infinity',b'-Infinity',b'{} garbage',b'"a\x00b"',b'-9223372036854775808',b'9223372036854775808',b'18446744073709551616',b'-18446744073709551617',b' [ true, false, null, -0 ] ']:
 try:r.parse(raw)
 except (ValueError,UnicodeError):pass
r.canonical({chr(c):chr(c) for c in range(32)})
r.canonical({'é':'e\u0301','😀':'\u0000','\ue000':0,'𐀀':1})
# Explicit preimage keeps bytecode opaque including NUL and non-UTF8 bytes.
m=t.FormatTests().module()
for b in [b'',b'\0\xff\x80',bytes(range(256))]: r.main_hash(m,b)
def attempt(fn,*args):
 try: fn(*args)
 except (ValueError,UnicodeError): pass
group='vectors'
for path in sorted((root/'docs/module-format-v1/vectors').glob('*.json')):
 attempt(r.parse,path.read_bytes())
 if path.name.endswith('.module.json'):
  r.normalize(original['parse'](path.read_bytes()))
 elif path.name in ['target.json','native-description.json']:
  r.normalize(original['parse'](path.read_bytes()),'target' if path.name=='target.json' else 'native')
r.normalize(original['parse']((root/'docs/module-format-v1/vectors/empty.gmod').read_bytes()),'gmod')
group='metadata_edges'
m=original['parse']((root/'docs/module-format-v1/vectors/empty.module.json').read_bytes())
for key,values in {'name':['con','prn.txt','com9','lpt0','aux','nul','CON','a/b','a\\b','', 'é','a'*257], 'version':['con.txt','1.0',''], 'main':[-1,True,0,4294967296], 'mainHash':['A'*64,'f'*63,'f'*64], 'description':['','x\0y'], 'license':[''], 'homepage':['']}.items():
 for value in values:
  v=copy.deepcopy(m);v[key]=value;attempt(r.normalize,v)
for authors in [[],[{'name':'A','email':''}],[{'name':'A'},{'name':'A','email':''}],[{'name':'B'},{'name':'A'}]]:
 v=copy.deepcopy(m);v['authors']=authors;attempt(r.normalize,v)
for producers in [[],[{'name':'z','version':'1','buildId':'a'},{'name':'a','version':'2','buildId':'b'}],[{'name':'a','version':'1','buildId':'b'}]*2]:
 v=copy.deepcopy(m);v['producers']=producers;attempt(r.normalize,v)
for external in [{'mutable':True,'value':1,'type':'integer'}, {'mutable':False,'type':'integer','value':9223372036854775808}, {'mutable':False,'functionId':1}, {'mutable':False,'type':'integer','functionId':1}, {'mutable':False,'functionId':1,'functionRef':{'module':'a','version':'1','name':'f'}}, {'mutable':False,'type':'null','value':None}, {'mutable':False,'functionRef':{'module':'con','version':'1','name':'f'}}]:
 v=copy.deepcopy(m);v['externals']={'x':external};attempt(r.normalize,v)
for field,value in [('files',[{'name':'x','entry':0,'external':False}]*2),('files',[{'name':'a\0b','entry':0,'external':False}]),('functions',[{'functionId':1,'file':1,'instruction':0,'parameters':[]}])]:
 v=copy.deepcopy(m);v[field]=value;attempt(r.normalize,v)
group='target_edges'
target=original['parse']((root/'docs/module-format-v1/vectors/target.json').read_bytes())
for key,values in {'architecture':['x86','aarch64','oops'],'bits':[32,True,64], 'cpuFeatures':[['z','a'],['sse2','sse2'],[]],'minimumOs':[[0],[10,0,0,0],[1,2,0,0],[],[-1],[True]]}.items():
 for value in values:
  v=copy.deepcopy(target);v[key]=value;attempt(r.normalize,v,'target')
for os in ['windows','linux']:
 for family in ['glibc','musl','ucrt','msvcrt']:
  for version in [[9],[10,0,0,0]]:
   v=copy.deepcopy(target);v['os']=os;v['runtime']['family']=family;v['runtime']['minimumVersion']=[2,0,0];v['minimumOs']=version;attempt(r.normalize,v,'target')
group='guard_edges'
def guard(kind,op,value):
 return {'argumentTypes':[kind],'conditions':[{'argument':0,'operation':op,'constant':{'type':kind,'value':value}}]}
for kind,values in [('integer',[-(1<<63),-1,0,1,(1<<63)-1]),('real',['0000000000000000','8000000000000000','7ff0000000000000','fff0000000000000','7ff8000000000001','0000000000000001']),('character',['\0','\ud7ff','\ue000','😀','\U0010ffff']),('string',['','x','x\0y','😀']),('boolean',[False,True]),('null',[None])]:
 operations=['equal','notEqual']+(['less','lessEqual','greater','greaterEqual'] if kind in ['integer','real','character'] else [])
 for op in operations:
  for i,value in enumerate(values):
   r.guards_overlap(guard(kind,op,value),guard(kind,'equal',values[(i+1)%len(values)]))
   r.guards_overlap(guard(kind,op,value),guard(kind,'equal',value))
# Native semantic failures in addition to direct overlap witness coverage.
native=original['parse']((root/'docs/module-format-v1/vectors/native-description.json').read_bytes())
for mutate in [lambda n:n['specializations'][0].update(symbol='wrong'),lambda n:n['specializations'][0]['signature'].update(arguments=['real','integer']),lambda n:n['specializations'][0]['when']['conditions'][0].update(argument=2),lambda n:n['specializations'][0]['when']['conditions'].append(copy.deepcopy(n['specializations'][0]['when']['conditions'][0])),lambda n:n['specializations'].append(copy.deepcopy(n['specializations'][0]))]:
 n=copy.deepcopy(native);mutate(n);attempt(r.normalize,n,'native')

counts={}
for row in records:
 counts[row['group']]=counts.get(row['group'],0)+1
 row['id']=f"{row['group']}-{counts[row['group']]}"
path=root/'test/data/format-oracle/cases.json'
path.write_text(json.dumps(records,ensure_ascii=True,indent=2)+'\n')
print(counts); print(len(records),hashlib.sha256(path.read_bytes()).hexdigest())
```

## Retained Python consumers

`reference.py`, `schema.py` and `test_reference.py` remain unchanged because
subsequent migrations still depend on them. Direct consumers at this step:

- `docs/module-format-v1/test_reference.py`
- `scripts/check_inspect_probe.py`
- `scripts/check_module_canonical.py`
- `scripts/check_module_consistency.py`
- `scripts/check_module_container.py`
- `scripts/check_module_delivery.py`
- `scripts/check_module_hash.py`
- `scripts/check_module_import_execution.py`
- `scripts/check_module_inspect.py`
- `scripts/check_module_loading.py`
- `scripts/check_module_memory.py`
- `scripts/check_module_merge.py`
- `scripts/check_module_musl.py`
- `scripts/check_module_search.py`
- `scripts/check_module_snapshot.py`
- `scripts/check_module_validation.py`
- `scripts/check_module_writer.py`

Frozen-sample tools also load the reference by `importlib`; they remain tracked
by #330/#395. Consumer removal is deferred until each owning checklist task.
