#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# Source at the start of a project CI step. This is a dependency guard, not a
# security sandbox: host interpreters remain outside the audited project routes.
# Never change installed interpreters, GitHub action environments, or permissions.
if [[ ${GOAT_NO_PYTHON_ACTIVE:-} == 1 ]]; then
    return 0
fi
_goat_no_python_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
_goat_no_python_previous_exit="$(trap -p EXIT)"
_goat_no_python_previous_command=""
if [[ -n $_goat_no_python_previous_exit ]]; then
    _goat_no_python_previous_exit="${_goat_no_python_previous_exit#trap -- }"
    eval "_goat_no_python_previous_command=${_goat_no_python_previous_exit% EXIT}"
fi
_goat_no_python_suffix=""
_goat_no_python_temp="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        _goat_no_python_suffix=.exe
        _goat_no_python_temp="$(cygpath -u "$_goat_no_python_temp")"
        ;;
esac
_goat_no_python_bin="$(mktemp -d "$_goat_no_python_temp/goat-no-python.XXXXXX")"
_goat_no_python_evidence="${GOAT_NO_PYTHON_EVIDENCE:-$_goat_no_python_root/build/python-free}/$(basename "$_goat_no_python_bin")"
mkdir -p "$_goat_no_python_evidence"
_goat_no_python_evidence="$(cd "$_goat_no_python_evidence" && pwd)"
_goat_no_python_attempts="$_goat_no_python_evidence/attempts.log"
: > "$_goat_no_python_attempts"
_goat_no_python_assert_empty() {
    if [[ -s $1 ]]; then
        cat "$1" >&2
        echo 'Python-free guard: forbidden interpreter attempt, even if its error was ignored.' >&2
        return 1
    fi
}
_goat_no_python_finish() {
    local status=$? audit_status=0
    trap - EXIT
    set +e
    cmake "-DSOURCE=$_goat_no_python_root" \
        "-DBUILDS=${BUILD_DIR:-};${GOAT_NO_PYTHON_BUILDS:-}" \
        -P "$_goat_no_python_root/scripts/check_no_python.cmake" \
        > "$_goat_no_python_evidence/audit-final.log" 2>&1 || audit_status=$?
    cat "$_goat_no_python_evidence/audit-final.log"
    if (( audit_status != 0 )); then
        (( status != 0 )) || status=$audit_status
    fi
    if [[ ! -f $_goat_no_python_attempts || ! -r $_goat_no_python_attempts || ! -w $_goat_no_python_attempts ]]; then
        echo 'Python-free attempt log is missing or inaccessible' >&2
        (( status != 0 )) || status=1
    fi
    if [[ -n ${_goat_no_python_expected_log:-} && ${GOAT_NO_PYTHON_LOG:-} != "$_goat_no_python_expected_log" ]]; then
        echo 'Python-free guard log environment was changed' >&2
        (( status != 0 )) || status=1
    fi
    if [[ -s $_goat_no_python_bin/invoked ]]; then
        cp "$_goat_no_python_bin/invoked" "$_goat_no_python_evidence/invoked"
        echo 'Python-free native marker detected a forbidden invocation' >&2
        (( status != 0 )) || status=1
    fi
    _goat_no_python_assert_empty "$_goat_no_python_attempts" || { (( status != 0 )) || status=1; }
    printf 'Project step exit status: %s\n' "$status" >> "$_goat_no_python_evidence/result.log"
    # Evidence is outside this temporary alias directory and survives cleanup.
    rm -rf -- "$_goat_no_python_bin" || { (( status != 0 )) || status=1; }
    if [[ -n $_goat_no_python_previous_command ]]; then
        # Reinstalling EXIT while already inside EXIT does not execute it.
        # Invoke the saved body explicitly with the final status in $?.
        (exit "$status")
        eval "$_goat_no_python_previous_command"
    fi
    exit "$status"
}
trap _goat_no_python_finish EXIT

"${CC:-gcc}" -std=c11 -Wall -Wextra -Werror \
    "$_goat_no_python_root/test/support/python_forbidden.c" \
    -o "$_goat_no_python_bin/forbidden$_goat_no_python_suffix"
# Explicit conventional aliases plus every matching basename already on PATH.
# The version range is bounded and documented; discovery covers newer installed
# aliases without claiming arbitrary renamed/absolute host binaries are sandboxed.
_goat_no_python_names=(python python2 python3 pythonw pythonw2 pythonw3 py pyw pypy pypy2 pypy3)
for _goat_major in 2 3; do
    for _goat_minor in {0..30}; do
        _goat_no_python_names+=("python$_goat_major.$_goat_minor" "pythonw$_goat_major.$_goat_minor" "pypy$_goat_major.$_goat_minor")
    done
done
IFS=: read -r -a _goat_no_python_paths <<< "$PATH"
for _goat_dir in "${_goat_no_python_paths[@]}"; do
    [[ -d $_goat_dir ]] || continue
    while IFS= read -r -d '' _goat_file; do
        _goat_name="${_goat_file##*/}"
        if [[ ${_goat_name,,} =~ ^(pythonw?|pypyw?)([0-9]+([.][0-9]+)*)?([.]exe)?$|^pyw?([.]exe)?$ ]]; then
            # Preserve POSIX case and the exact discovered spelling, including
            # unusual uppercase .EXE suffixes on a case-sensitive filesystem.
            cp "$_goat_no_python_bin/forbidden$_goat_no_python_suffix" "$_goat_no_python_bin/$_goat_name"
            _goat_no_python_names+=("${_goat_name%.[eE][xX][eE]}")
            printf '%s\n' "$_goat_file" >> "$_goat_no_python_evidence/host-path-inventory.log"
        fi
    done < <(find "$_goat_dir" -maxdepth 1 \( -type f -o -type l \) -print0)
done
for _goat_name in "${_goat_no_python_names[@]}"; do
    # Native .exe sentinels are necessary for cmd/CreateProcess, not just Bash.
    cp "$_goat_no_python_bin/forbidden$_goat_no_python_suffix" "$_goat_no_python_bin/$_goat_name$_goat_no_python_suffix"
    if [[ -z $_goat_no_python_suffix ]]; then
        cp "$_goat_no_python_bin/forbidden" "$_goat_no_python_bin/$_goat_name.exe"
    fi
done
export PATH="$_goat_no_python_bin:$PATH"
export GOAT_NO_PYTHON_ACTIVE=1
export GOAT_NO_PYTHON_LOG="$_goat_no_python_evidence/negative-controls.log"
if [[ -n $_goat_no_python_suffix ]]; then
    GOAT_NO_PYTHON_LOG="$(cygpath -w "$GOAT_NO_PYTHON_LOG")"
fi
hash -r
_goat_control_status=0
for _goat_name in python python3 python3.13 pythonw py pyw pypy3; do
    _goat_control_status=0
    "$_goat_name" --version 2>> "$_goat_no_python_evidence/negative-controls.stderr" || _goat_control_status=$?
    [[ $_goat_control_status == 99 ]] || { echo "Poison alias failed: $_goat_name ($_goat_control_status)" >&2; exit 1; }
done
_goat_control_status=0
"$_goat_no_python_bin/python$_goat_no_python_suffix" --version 2>> "$_goat_no_python_evidence/negative-controls.stderr" || _goat_control_status=$?
[[ $_goat_control_status == 99 ]] || exit 1
if [[ -n $_goat_no_python_suffix ]]; then
    _goat_control_status=0
    MSYS2_ARG_CONV_EXCL='*' cmd.exe /d /c 'python.exe --version' 2>> "$_goat_no_python_evidence/negative-controls.stderr" || _goat_control_status=$?
    [[ $_goat_control_status == 99 ]] || { echo 'Native cmd poison control failed' >&2; exit 1; }
fi
# Swallow the exit status deliberately, then exercise the same final-log check.
bash -c 'python --version 2>/dev/null || :'
[[ -s "$_goat_no_python_evidence/negative-controls.log" ]] || exit 1
if _goat_no_python_assert_empty "$_goat_no_python_evidence/negative-controls.log" \
    2> "$_goat_no_python_evidence/ignored-error-control.log"; then
    echo 'Ignored-error detection control failed' >&2
    exit 1
fi
# Audit negative controls include absolute POSIX/Windows interpreter references
# and generated CTest command/environment metadata. Do not execute real Python.
for _goat_case in shell cmake environment c c-script pyw shebang; do
    case $_goat_case in
        shell) printf '/usr/bin/python3 --version\n' > "$_goat_no_python_bin/control.sh"; _goat_input=control.sh ;;
        cmake) printf 'add_test(bad "C:/tools/python.exe" -V)\n' > "$_goat_no_python_bin/control.cmake"; _goat_input=control.cmake ;;
        environment) printf 'set_tests_properties(bad PROPERTIES ENVIRONMENT "Python3_EXECUTABLE=/usr/bin/other")\n' > "$_goat_no_python_bin/control.cmake"; _goat_input=control.cmake ;;
        c) printf 'const char *command = "/opt/python3/bin/python3";\n' > "$_goat_no_python_bin/control.c"; _goat_input=control.c ;;
        c-script) printf 'system("/usr/bin/python3 tool.py");\n' > "$_goat_no_python_bin/control.c"; _goat_input=control.c ;;
        pyw) printf 'add_test(bad "C:/Windows/pyw.exe" -c ignored)\n' > "$_goat_no_python_bin/control.cmake"; _goat_input=control.cmake ;;
        shebang) printf '#!/usr/bin/python3\n' > "$_goat_no_python_bin/control.sh"; _goat_input=control.sh ;;
    esac
    if cmake "-DINPUTS=$_goat_no_python_bin/$_goat_input" \
        -P "$_goat_no_python_root/scripts/check_no_python.cmake" \
        > "$_goat_no_python_evidence/audit-control-$_goat_case.log" 2>&1; then
        echo "Static audit negative control failed: $_goat_case" >&2
        exit 1
    fi
done
# Missing detail-log environment must still leave the independent native marker.
rm -- "$_goat_no_python_bin/invoked"
_goat_control_status=0
env -u GOAT_NO_PYTHON_LOG "$_goat_no_python_bin/python$_goat_no_python_suffix" --version \
    2>> "$_goat_no_python_evidence/negative-controls.stderr" || _goat_control_status=$?
[[ $_goat_control_status == 98 && -s $_goat_no_python_bin/invoked ]] || exit 1
mv "$_goat_no_python_bin/invoked" "$_goat_no_python_evidence/missing-log-control.marker"
export GOAT_NO_PYTHON_LOG="$_goat_no_python_attempts"
if [[ -n $_goat_no_python_suffix ]]; then
    GOAT_NO_PYTHON_LOG="$(cygpath -w "$GOAT_NO_PYTHON_LOG")"
fi
_goat_no_python_expected_log="$GOAT_NO_PYTHON_LOG"
cmake "-DSOURCE=$_goat_no_python_root" -P "$_goat_no_python_root/scripts/check_no_python.cmake" \
    > "$_goat_no_python_evidence/audit-source.log" 2>&1
cat "$_goat_no_python_evidence/audit-source.log"
printf 'Python-free scoped guard active; evidence: %s\n' "$_goat_no_python_evidence"
