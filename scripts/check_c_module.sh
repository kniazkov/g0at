#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
set -Eeuo pipefail
interpreter="${1:?goat executable required}"
output_dir="${2:?output directory required}"
mkdir -p "$output_dir"
passed=0
current='module export'
printf 'Starting C module testing...\n'
trap 'printf "[fail] %s\nC module testing done; total: %d, passed: %d, failed: 1\n" "$current" "$((passed+1))" "$passed"' ERR
ok() {
    printf '[ ok ] %s\n' "$current"
    passed=$((passed+1))
}
compile() {
    "${CC:-gcc}" -std=c11 -Wall -Wextra -Werror -pedantic-errors -O2 "$@"
}
cat > "$output_dir/recursive sample.goat" <<'GOAT'
const fib=func(n){if(n<1)return 0;if(n==1)return 1;return fib(n-1)+fib(n-2);};
const twice=func(n){return n+n;};
const mixed=func(n){if(n<0.5)return n+1;return n;};
twice(1);twice(1.5);mixed(1);fib(2);
print("THIS MUST NOT RUN");
throw "NEITHER SHOULD THIS";
GOAT
"$interpreter" --lang en --print-c --save-c "$output_dir/recursive sample.goat" > "$output_dir/printed.c" 2> "$output_dir/errors.txt"
test ! -s "$output_dir/errors.txt"
# Both stdout and saved files use the platform text-mode newline convention.
cmp "$output_dir/printed.c" "$output_dir/recursive sample.c"
"$interpreter" --print-c "$output_dir/recursive sample.goat" > "$output_dir/repeated.c"
cmp "$output_dir/printed.c" "$output_dir/repeated.c"
test "$(grep -c '#include <stdint.h>' "$output_dir/printed.c")" = 1
ok
current='compile and execute exported recursive specializations'
cat > "$output_dir/driver.c" <<'C'
#include "recursive sample.c"
int main(void) {
    return g_f1_i_(10)!=55 || g_f1_i_(11)!=89 ||
           g_f2_i_(4)!=8 || g_f2_r_(0.25)!=0.5 ||
           g_f3_i_(0)!=1 || g_f3_i_(2)!=2;
}
C
compile -c "$output_dir/recursive sample.c" -o "$output_dir/module.o"
compile "$output_dir/driver.c" -lm -o "$output_dir/driver.exe"
"$output_dir/driver.exe"
ok
current='save-only mode and analysis sidecar'
"$interpreter" --save-c --save-analysis "$output_dir/report.txt" "$output_dir/recursive sample.goat" > "$output_dir/stdout.txt"
test ! -s "$output_dir/stdout.txt"
test -s "$output_dir/report.txt"
ok
current='exclude ineligible recursion and retain independent functions'
cat > "$output_dir/partial.goat" <<'GOAT'
const a=func(n){if(n<1)return n%2;return b(n-1);};
const b=func(n){if(n<1)return 0;return a(n-1);};
const caller=func(n){return b(n);};
const independent=func(n){return n+1;};
a(0);b(0);caller(0);independent(1);
GOAT
"$interpreter" --lang en --print-c "$output_dir/partial.goat" > "$output_dir/partial.c" 2> "$output_dir/partial.log"
test ! -s "$output_dir/partial.log"
if grep -Eq 'g_f[123]_' "$output_dir/partial.c"; then false; fi
cat > "$output_dir/partial_driver.c" <<'C'
#include "partial.c"
int main(void) { return g_f4_i_(41)!=42; }
C
compile -c "$output_dir/partial.c" -o "$output_dir/partial.o"
compile "$output_dir/partial_driver.c" -lm -o "$output_dir/partial.exe"
"$output_dir/partial.exe"
ok
current='empty module is a valid translation unit'
printf 'print("DO NOT RUN");\n' > "$output_dir/empty.goat"
"$interpreter" --print-c "$output_dir/empty.goat" > "$output_dir/empty.c"
compile -c "$output_dir/empty.c" -o "$output_dir/empty.o"
ok
current='emit only the required integer operation helpers'
for operation in add sub mul neg plus real; do
    case "$operation" in
        add) expression='a+b'; seed='f(1,2);' ;;
        sub) expression='a-b'; seed='f(1,2);' ;;
        mul) expression='a*b'; seed='f(1,2);' ;;
        neg) expression='-a'; seed='f(1,2);' ;;
        plus) expression='+a'; seed='f(1,2);' ;;
        real) expression='a+b'; seed='f(1.5,2.5);' ;;
    esac
    printf 'const f=func(a,b){return %s;};%s\n' "$expression" "$seed" > "$output_dir/$operation.goat"
    "$interpreter" --print-c "$output_dir/$operation.goat" > "$output_dir/$operation.c"
    compile -c "$output_dir/$operation.c" -o "$output_dir/$operation.o"
    for helper in add sub mul neg; do
        if test "$helper" = "$operation"; then
            test "$(grep -c "static inline int64_t g_i64_$helper(" "$output_dir/$operation.c")" = 1
            test "$(grep -c "= g_i64_$helper(" "$output_dir/$operation.c")" = 1
        elif grep -q "g_i64_$helper(" "$output_dir/$operation.c"; then
            false
        fi
    done
    if test "$operation" = plus || test "$operation" = real; then
        if grep -q 'g_i64_bits' "$output_dir/$operation.c"; then false; fi
    fi
done
ok
current='invalid export options'
for option in '--optimize none' '--print-bytecode' '--print-analysis' '--print-source-code'; do
    # Each case intentionally consists of one or two separate arguments.
    if "$interpreter" --print-c $option "$output_dir/empty.goat" > "$output_dir/stdout.txt" 2> "$output_dir/error.txt"; then false; fi
    test ! -s "$output_dir/stdout.txt"
    test -s "$output_dir/error.txt"
done
ok
current='write failures and input protection'
mkdir -p "$output_dir/unwritable.c"
printf 'print(1);\n' > "$output_dir/unwritable.goat"
if "$interpreter" --save-c "$output_dir/unwritable.goat" > "$output_dir/stdout.txt" 2> "$output_dir/error.txt"; then false; fi
test -s "$output_dir/error.txt"
printf 'print(1);\n' > "$output_dir/source.c"
cp "$output_dir/source.c" "$output_dir/source.before"
if "$interpreter" --save-c "$output_dir/source.c" > "$output_dir/stdout.txt" 2> "$output_dir/error.txt"; then false; fi
cmp "$output_dir/source.c" "$output_dir/source.before"
ok
current='parser errors do not create C output'
printf 'var =;\n' > "$output_dir/bad.goat"
if "$interpreter" --save-c "$output_dir/bad.goat" > "$output_dir/stdout.txt" 2> "$output_dir/error.txt"; then false; fi
test ! -e "$output_dir/bad.c"
test -s "$output_dir/error.txt"
ok
current='replacement proofs survive compilation and unobserved arguments'
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
"$interpreter" --print-c "$repo_root/test/functional/native_modes/replacement.goat" > "$output_dir/replacement.c"
cat > "$output_dir/replacement_driver.c" <<'C'
#include "replacement.c"
int main(void) {
    return g_f1_i_(100)!=105 || g_f1_r_(0.25)!=5.25 ||
           g_f2_i_(-10)!=-9 || g_f3_i_(-7)!=-7 ||
           g_f4_i_(INT64_MAX)!=INT64_MAX ||
           g_f4_r_(-0.25)!=4.75 || g_f4_r_(0x1p63)!=0x1p63 ||
           !isnan(g_f4_r_(NAN)) || g_f4_r_(INFINITY)!=INFINITY ||
           g_f5_i_(100)!=2 || g_f6_i_(-10)!=-4 ||
           !signbit(g_f7_i_(0)) || g_f8_i_(-1)!=INT64_MAX-1;
}
C
compile -c "$output_dir/replacement.c" -o "$output_dir/replacement.o"
compile "$output_dir/replacement_driver.c" -lm -o "$output_dir/replacement.exe"
"$output_dir/replacement.exe"
ok
current='deleted bindings stay absent from generated C while initializers still execute'
cat > "$output_dir/deleted.goat" <<'GOAT'
const f=func(n){var x=20;return n*1.0;};
const chain=func(n){var a=20;var b=a;return n;};
const assigned=func(n){var unused=0;return unused=n+1;};
const helper=func(n){return n+1;};
const kept_call=func(n){var unused=helper(n);return n;};
const kept_argument=func(n){var unused=(n=n+1);return n;};
const observed=func(n){var x=n+1;return x;};
const nested=func(n){var x=n;return x=(x=1)+2;};
f(10);f(0.5);chain(10);assigned(10);helper(10);kept_call(10);
kept_argument(10);observed(10);observed(0.5);nested(10);
GOAT
"$interpreter" --print-c "$output_dir/deleted.goat" > "$output_dir/deleted.c"
for function in g_f1_i_ g_f1_r_ g_f2_i_ g_f3_i_ g_f5_i_ g_f6_i_; do
    # Inspect only the function definition, excluding adapters and helper functions.
    sed -n "/^__attribute__((noinline)).* $function(/,/^}/p" "$output_dir/deleted.c" > "$output_dir/body.c"
    test -s "$output_dir/body.c"
    if grep -q 'g_l[0-9]' "$output_dir/body.c"; then false; fi
done
sed -n '/^__attribute__((noinline)).* g_f1_i_(/,/^}/p' "$output_dir/deleted.c" > "$output_dir/body.c"
if grep -q 'INT64_C(20)' "$output_dir/body.c"; then false; fi
sed -n '/^__attribute__((noinline)).* g_f5_i_(/,/^}/p' "$output_dir/deleted.c" > "$output_dir/body.c"
grep -q 'g_f4_i_(' "$output_dir/body.c"
cat > "$output_dir/deleted_driver.c" <<'C'
#include "deleted.c"
int main(void) {
    return g_f1_i_(-7)!=-7.0 || g_f1_r_(0.25)!=0.25 ||
           !isnan(g_f1_r_(NAN)) || !signbit(g_f1_r_(-0.0)) ||
           g_f2_i_(INT64_MIN)!=INT64_MIN || g_f3_i_(INT64_MAX)!=INT64_MAX ||
           g_f5_i_(9)!=9 || g_f6_i_(9)!=10 ||
           g_f7_i_(41)!=42 || g_f7_r_(0.25)!=1.25 || g_f8_i_(9)!=3;
}
C
compile "$output_dir/deleted_driver.c" -lm -o "$output_dir/deleted.exe"
"$output_dir/deleted.exe"
ok
printf 'C module testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
