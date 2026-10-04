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
    return goat_f1_i_(10)!=55 || goat_f1_i_(11)!=89 ||
           goat_f2_i_(4)!=8 || goat_f2_r_(0.25)!=0.5 ||
           goat_f3_i_(0)!=1 || goat_f3_i_(2)!=2;
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
if grep -Eq 'goat_f[123]_' "$output_dir/partial.c"; then false; fi
cat > "$output_dir/partial_driver.c" <<'C'
#include "partial.c"
int main(void) { return goat_f4_i_(41)!=42; }
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
            test "$(grep -c "static inline int64_t goat_i64_$helper(" "$output_dir/$operation.c")" = 1
            test "$(grep -c "= goat_i64_$helper(" "$output_dir/$operation.c")" = 1
        elif grep -q "goat_i64_$helper(" "$output_dir/$operation.c"; then
            false
        fi
    done
    if test "$operation" = plus || test "$operation" = real; then
        if grep -q 'goat_i64_bits' "$output_dir/$operation.c"; then false; fi
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
    return goat_f1_i_(100)!=105 || goat_f1_r_(0.25)!=5.25 ||
           goat_f2_i_(-10)!=-9 || goat_f3_i_(-7)!=-7 ||
           goat_f4_i_(INT64_MAX)!=INT64_MIN+4 ||
           goat_f4_r_(-0.25)!=4.75 || goat_f4_r_(0x1p63)!=0x1p63 ||
           !isnan(goat_f4_r_(NAN)) || goat_f4_r_(INFINITY)!=INFINITY ||
           goat_f5_i_(100)!=2 || goat_f6_i_(-10)!=-4 ||
           !signbit(goat_f7_i_(0)) || goat_f8_i_(-1)!=INT64_MAX;
}
C
compile -c "$output_dir/replacement.c" -o "$output_dir/replacement.o"
compile "$output_dir/replacement_driver.c" -lm -o "$output_dir/replacement.exe"
"$output_dir/replacement.exe"
ok
printf 'C module testing done; total: %d, passed: %d, failed: 0\n' "$passed" "$passed"
