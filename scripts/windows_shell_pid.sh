# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# Source this file: $$ must identify the calling MSYS2 shell, not a helper.
windows_shell_pid() {
    local native_pid
    native_pid=$(ps -p "$$" | awk '
        NR == 1 { for (i = 1; i <= NF; i++) if ($i == "WINPID") column = i }
        NR > 1 && column { print $column }
    ')
    if [[ ! "$native_pid" =~ ^[1-9][0-9]*$ ]]; then
        printf 'Cannot resolve native Windows PID of MSYS2 shell %s\n' "$$" >&2
        return 1
    fi
    printf '%s\n' "$native_pid"
}
