# Copyright 2026 Ivan Kniazkov
"""AC-23–AC-28 through real file scopes and independently compiled modules."""


def run(suite):
    # AC-23: observation is performed with the executing function's rights.
    # A sibling file, a private-domain method, and an importing module all see
    # different views of the same object, including nested formatting and keys.
    for operation in ['clone', 'flat']:
        p = suite.project('ac23-observation-' + operation)
        p.compile('library', '''
            const impl = include "impl.goat";
            const observer = include "observer.goat";
            external box = impl.box;
            external sibling = observer.observe;
        ''', files={
            'impl.goat': '''
                public box = {
                    var fileOnly = 10;
                    public moduleOnly = 20;
                    external shown = 30;
                    private secret = 40;
                    protected shield = 50;
                    external inspect = func(value, callback) {
                        println(str(value));
                        var keys = [];
                        for (key in value) keys.push(key);
                        println(keys);
                        println([value]);
                        callback(value);
                        println(str(value));
                    };
                };
            ''',
            'observer.goat': '''
                public observe = func(value) {
                    println(str(value));
                    var keys = [];
                    for (key in value) keys.push(key);
                    println(keys);
                };
            '''})
        p.compile('facade', 'const lib = import "library@1"; external box = lib.box; '
                  'external sibling = lib.sibling;')
        outside = '{"inspect":func,"shown":30}\n'
        outside_keys = '["inspect","shown"]\n'
        own = '{"fileOnly":10,"inspect":func,"moduleOnly":20,"secret":40,"shield":50,"shown":30}\n'
        own_keys = '["fileOnly","inspect","moduleOnly","secret","shield","shown"]\n'
        sibling = '{"inspect":func,"moduleOnly":20,"shown":30}\n'
        sibling_keys = '["inspect","moduleOnly","shown"]\n'
        # The original method keeps its file/module origin, but gains no private
        # or protected-domain right over a clone owned by the importing module.
        original_on_copy = '{"fileOnly":10,"inspect":func,"moduleOnly":20,"shown":30}\n'
        original_keys = '["fileOnly","inspect","moduleOnly","shown"]\n'
        p.run('''
            const lib = import "library@1";
            const facade = import "facade@1";
            println(lib.box == facade.box);
            const outside = func(value) {
                println(value);
                var keys = [];
                for (key in value) keys.push(key);
                println(keys);
            };
            outside(facade.box);
            lib.sibling(facade.box);
            facade.box.inspect(facade.box, outside);
            const copy = facade.box.''' + operation + '''();
            copy.inspect(copy, outside);
            facade.box.inspect(copy, outside);
            try { copy.inspect(copy, func(value) { throw "stop"; }); }
            catch(e) { println(e); }
            outside(copy);
        ''', 'true\n' + outside + outside_keys + sibling + sibling_keys
              + own + own_keys + '[' + own.rstrip('\n') + ']\n'
              + outside + outside_keys + own
              + own + own_keys + '[' + own.rstrip('\n') + ']\n'
              + outside + outside_keys + own
              + original_on_copy + original_keys + '[' + original_on_copy.rstrip('\n') + ']\n'
              + outside + outside_keys + original_on_copy
              + own + own_keys + '[' + own.rstrip('\n') + ']\nstop\n'
              + outside + outside_keys)

    # AC-23: key iteration snapshots accessible own keys. Hidden object keys
    # and nested cyclic values are filtered using the same outside observer.
    p = suite.project('ac23-snapshot-and-recursion')
    p.compile('library', 'external box = (include "impl.goat").box; '
              'external hidden = (include "impl.goat").hidden;', files={
        'impl.goat': '''public box = {
            private secret = 42;
            external var a = 1; external var b = 2;
            external add = func() { this.c = 3; b = 20; };
        };
        public hidden = { private secret = 99; };'''})
    p.run('''const lib = import "library@1";
        const copy = lib.box.flat();
        var keys = [];
        for (key in copy) {
            keys.push(key);
            if (key == "a") { copy.extra = 3; copy.b = 20; }
        }
        println(keys); println(copy);
        var dictionary = { };
        dictionary[lib.hidden] = [lib.hidden]; println(dictionary);
        var cyclic = { private secret = 99; };
        cyclic.self = [cyclic]; println(cyclic);
        println(lib.hidden);
    ''', '["a","add","b"]\n{"a":1,"add":func,"b":20,"extra":3}\n'
          '{{ }:[{ }]}\n{"self":[{...}]}\n{ }\n')

    # AC-24: a private field does not make its returned array immutable. Copying
    # the array isolates elements, not nested values or the private field cell.
    p = suite.project('ac24-array')
    p.compile('library', 'external box = (include "impl.goat").box;', files={
        'impl.goat': '''public box = {
            private var items = [1];
            private var nested = [{ external var value = 7; }];
            external getNested = func() { return nested.clone(); };
            external get = func() { return items; };
            external copy = func() { return items.clone(); };
        };'''})
    p.run('''const lib = import "library@1";
        const shared = lib.box.get(); shared.push(2);
        const separate = lib.box.copy(); separate.push(3);
        println(lib.box.get()); println(separate);
        println(shared == lib.box.get()); println(separate == shared);
        try { println(lib.box.items); } catch(e) { println(e); }
        try { lib.box.items = [9]; } catch(e) { println(e); }
        println(lib.box.get());
        const nested = lib.box.getNested(); nested[0].value = 8;
        println(lib.box.getNested()[0].value);
    ''', '[1,2]\n[1,2,3]\ntrue\nfalse\nACCESS_DENIED\nACCESS_DENIED\n[1,2]\n8\n')

    # AC-25/26: object ownership follows the factory's executing module; a copy
    # belongs to the caller but its hidden fields and methods retain provenance.
    for operation in ['clone', 'flat']:
        p = suite.project('ac25-26-factory-' + operation)
        p.compile('library', 'external make = (include "factory.goat").make;', files={
            'factory.goat': '''public make = func() { return {
                var fileOnly = 7;
                public moduleOnly = 8;
                external var x = 1;
                external read = func() { return fileOnly + moduleOnly; };
                external change = func(value) { fileOnly = value; };
            }; };'''})
        p.compile('facade', 'external make = (import "library@1").make;')
        p.run('''const lib = import "library@1";
            const facade = import "facade@1";
            println(lib.make == facade.make);
            const obj = facade.make(); obj.x = 2; println(obj.x);
            try { obj.extra = 3; } catch(e) { println(e); }
            const copy = obj.''' + operation + '''(); copy.extra = 3;
            const child = obj -> { public own = 4; };
            println(copy.extra); println(child.own); println(copy.read());
            copy.change(20); println(copy.read()); println(obj.read());
            const read = func(value, key) { return value[key]; };
            for (value in [obj, copy, child]) {
                try { read(value, "fileOnly"); } catch(e) { println(e); }
                try { read(value, "moduleOnly"); } catch(e) { println(e); }
            }
        ''', 'true\n2\nFOREIGN_OBJECT\n3\n4\n15\n28\n15\n'
              + 'ACCESS_DENIED\n' * 6)

    # AC-27: public/external do not weaken protected/private domain checks.
    p = suite.project('ac27-protected')
    p.compile('library', 'external Base = (include "base.goat").Base;', files={
        'base.goat': '''public Base = {
            protected var x = 10;
            private var secret = 20;
            external readSecret = func() { return secret; };
        };'''})
    p.compile('facade', 'external Base = (import "library@1").Base;')
    p.run('''const lib = import "facade@1";
        const child = lib.Base -> {
            public fromBody = x;
            public read = func() { return lib.Base.x; };
            public change = func(value) { lib.Base.x = value; };
            public forbidden = func() { return lib.Base.secret; };
        };
        println(child.fromBody); println(child.read()); println(child.readSecret());
        child.change(11); println(child.read());
        try { child.forbidden(); } catch(e) { println(e); }
        const read = func(value, key) { return value[key]; };
        try { read(lib.Base, "x"); } catch(e) { println(e); }
        try { read(lib.Base, "secret"); } catch(e) { println(e); }
        try { lib.Base.x = 99; } catch(e) { println(e); }
        println(child.read());
    ''', '10\n10\n20\n11\nACCESS_DENIED\nACCESS_DENIED\nACCESS_DENIED\nACCESS_DENIED\n11\n')

    # AC-28: distinguish hidden fields, visible const fields and object extension.
    for indexed in [False, True]:
        p = suite.project('ac28-const-' + str(indexed))
        p.compile('library', 'external obj = (include "impl.goat").obj;', files={
            'impl.goat': '''public obj = {
                external var writable = 1; external fixed = 2;
                public var internal = 3;
            };'''})
        lhs = 'obj[key]' if indexed else 'obj.fixed'
        p.run('''const lib = import "library@1";
            const obj = lib.obj; obj.writable = 3;
            println(obj.writable); println(obj.fixed);
            const set = func(obj, key) { ''' + lhs + ''' = 4; };
            try { set(obj, "fixed"); } catch(e) { println(e); }
            try { obj.newField = 5; } catch(e) { println(e); }
            try { obj.internal = 6; } catch(e) { println(e); }
            println(obj.writable); println(obj.fixed);
        ''', '3\n2\nPROPERTY_IS_CONSTANT\nFOREIGN_OBJECT\nACCESS_DENIED\n3\n2\n')
