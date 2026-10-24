/** @file file_system_windows.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Strict Unicode paths, native handle traversal and Windows access control.
 */
#include "file_system_internal.h"
#ifdef _WIN32
#    include "allocate.h"

#    include <aclapi.h>
#    include <bcrypt.h>
#    include <errno.h>
#    include <fcntl.h>
#    include <io.h>
#    include <shellapi.h>
#    include <string.h>
#    include <sys/stat.h>
#    include <windows.h>
#    include <winternl.h>

wchar_t *fs_wide(const char *text) {
    if (!text) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!n)
        return NULL;
    wchar_t *out = ALLOC((size_t)n * sizeof(*out));
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, n)) {
        FREE(out);
        return NULL;
    }
    return out;
}

char *fs_utf8(const wchar_t *text) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, NULL, 0, NULL, NULL);
    if (!n)
        return NULL;
    char *out = ALLOC(n);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, out, n, NULL, NULL)) {
        FREE(out);
        return NULL;
    }
    return out;
}

bool fs_native_error(fs_error_t *error, const char *operation, const char *path) {
    DWORD code = GetLastError();
    fs_status_t status =
        code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND              ? FS_NOT_FOUND
        : code == ERROR_ACCESS_DENIED || code == ERROR_PRIVILEGE_NOT_HELD         ? FS_ACCESS
        : code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS               ? FS_EXISTS
        : code == ERROR_NOT_SUPPORTED || code == ERROR_NOT_SAME_DEVICE            ? FS_UNSUPPORTED
        : code == ERROR_NO_UNICODE_TRANSLATION || code == ERROR_INVALID_PARAMETER ? FS_INVALID
                                                                                  : FS_IO;
    return fs_fail(error, status, code, operation, path);
}

FILE *FOPEN(const char *path, const char *mode) {
    if (!fs_valid(path, NULL)) {
        errno = EILSEQ;
        return NULL;
    }
    wchar_t *wide = fs_wide(path), *wm = fs_wide(mode);
    FILE *file = NULL;
    if (wide && wm) {
        if (!strcmp(mode, "wbx")) {
            int fd = _wopen(wide,
                            _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                            _S_IREAD | _S_IWRITE);
            if (fd >= 0) {
                file = _fdopen(fd, "wb");
                if (!file)
                    _close(fd);
            }
        } else
            file = _wfopen(wide, wm);
    }
    FREE(wide);
    FREE(wm);
    return file;
}

char *absolute_fs_path(const char *path, fs_error_t *error) {
    if (!fs_valid(path, error))
        return NULL;
    wchar_t *input = fs_wide(path);
    if (!input) {
        fs_native_error(error, "decode path", path);
        return NULL;
    }
    DWORD size = GetFullPathNameW(input, 0, NULL, NULL);
    char *out = NULL;
    if (size) {
        wchar_t *full = ALLOC((size_t)size * sizeof(*full));
        DWORD n = GetFullPathNameW(input, size, full, NULL);
        if (n && n < size)
            out = fs_utf8(full);
        else if (n)
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        FREE(full);
    }
    if (!out)
        fs_native_error(error, "absolute path", path);
    FREE(input);
    return out;
}

char *fs_environment(const char *name, fs_error_t *error) {
    if (!fs_valid(name, error))
        return NULL;
    wchar_t *wide = fs_wide(name);
    if (!wide) {
        fs_native_error(error, "environment name", name);
        return NULL;
    }
    SetLastError(ERROR_SUCCESS);
    DWORD size = GetEnvironmentVariableW(wide, NULL, 0);
    char *out = NULL;
    if (!size && GetLastError() == ERROR_SUCCESS) {
        out = ALLOC(1);
        *out = 0;
    }
    if (size) {
        wchar_t *value = ALLOC((size_t)size * sizeof(*value));
        SetLastError(ERROR_SUCCESS);
        DWORD n = GetEnvironmentVariableW(wide, value, size);
        if (n < size && (n || GetLastError() == ERROR_SUCCESS))
            out = fs_utf8(value);
        else if (n)
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        FREE(value);
    }
    if (!out) {
        DWORD code = GetLastError();
        fs_fail(error,
                code == ERROR_ENVVAR_NOT_FOUND ? FS_NOT_FOUND : FS_IO,
                code,
                "environment",
                name);
    }
    FREE(wide);
    return out;
}

char *temporary_fs_directory(fs_error_t *error) {
    DWORD size = GetTempPathW(0, NULL);
    char *out = NULL;
    if (size) {
        wchar_t *path = ALLOC(((size_t)size + 1) * sizeof(*path));
        DWORD n = GetTempPathW(size + 1, path);
        if (n && n <= size)
            out = fs_utf8(path);
        FREE(path);
    }
    if (!out)
        fs_native_error(error, "temporary directory", NULL);
    return out;
}

static HANDLE
open_path_flags(const char *path, DWORD access, bool follow, DWORD share, DWORD flags) {
    wchar_t *wide = fs_wide(path);
    if (!wide)
        return INVALID_HANDLE_VALUE;
    HANDLE handle = CreateFileW(wide,
                                access,
                                share,
                                NULL,
                                OPEN_EXISTING,
                                flags | (follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT),
                                NULL);
    DWORD saved = GetLastError();
    FREE(wide);
    SetLastError(saved);
    return handle;
}

static HANDLE open_path(const char *path, DWORD access, bool follow, DWORD share) {
    return open_path_flags(path, access, follow, share, FILE_FLAG_BACKUP_SEMANTICS);
}

static bool source_path_valid(const char *path, fs_error_t *error) {
    if (!fs_valid(path, error))
        return false;
    size_t length = strlen(path);
    bool slash = path[0] == '/' || path[0] == '\\';
    bool second_slash = length > 1 && (path[1] == '/' || path[1] == '\\');
    bool drive_relative =
        length > 1 && path[1] == ':' && (length == 2 || (path[2] != '/' && path[2] != '\\'));
    if ((slash && !second_slash) || drive_relative)
        return fs_fail(error,
                       FS_INVALID,
                       ERROR_INVALID_NAME,
                       "incomplete Windows source path",
                       path);
    /* Device namespaces are not source paths. Extended DOS/UNC paths are fine. */
    if (length >= 4 && slash && second_slash && path[2] == '.')
        return fs_fail(error, FS_INVALID, ERROR_INVALID_NAME, "device source path", path);
    return true;
}

char *canonical_fs_path(const char *path, fs_error_t *error) {
    if (!source_path_valid(path, error))
        return NULL;
    HANDLE handle = open_path(path,
                              FILE_READ_ATTRIBUTES,
                              true,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (handle == INVALID_HANDLE_VALUE) {
        fs_native_error(error, "open canonical path", path);
        return NULL;
    }
    DWORD count =
        GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    char *out = NULL;
    if (count) {
        wchar_t *wide = ALLOC(((size_t)count + 1) * sizeof(*wide));
        DWORD n = GetFinalPathNameByHandleW(handle,
                                            wide,
                                            count + 1,
                                            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (n && n <= count) {
            char *raw = fs_utf8(wide);
            if (raw) {
                const char *start = raw;
                if (!strncmp(raw, "\\\\?\\UNC\\", 8)) {
                    /* Keep UNC semantics while dropping the extended prefix. */
                    raw[6] = '/';
                    start = raw + 6;
                } else if (!strncmp(raw, "\\\\?\\", 4))
                    start = raw + 4;
                out = ALLOC(strlen(start) + 1);
                strcpy(out, start);
                for (char *p = out; *p; p++)
                    if (*p == '\\')
                        *p = '/';
                FREE(raw);
            }
        } else if (n)
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        FREE(wide);
    }
    if (!out)
        fs_native_error(error, "canonical path", path);
    if (!CloseHandle(handle) && out) {
        fs_native_error(error, "close canonical path", path);
        FREE(out);
        out = NULL;
    }
    return out;
}

bool read_fs_regular_file(const char *path,
                          size_t limit,
                          void **data,
                          size_t *size,
                          fs_error_t *error) {
    if (data)
        *data = NULL;
    if (size)
        *size = 0;
    if (!data || !size || limit == SIZE_MAX)
        return fs_fail(error,
                       FS_INVALID,
                       ERROR_INVALID_PARAMETER,
                       "regular file output/limit",
                       path);
    if (!source_path_valid(path, error))
        return false;
    /* Ordinary module/source reads must not opt into backup-privilege ACL bypass.
     * Directory metadata operations still need BACKUP_SEMANTICS to open directories. */
    HANDLE handle = open_path_flags(path,
                                    GENERIC_READ,
                                    true,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    0);
    if (handle == INVALID_HANDLE_VALUE)
        return fs_native_error(error, "open regular file", path);
    BY_HANDLE_FILE_INFORMATION info;
    bool ok = GetFileInformationByHandle(handle, &info) != 0;
    if (!ok)
        fs_native_error(error, "stat opened regular file", path);
    else if (GetFileType(handle) != FILE_TYPE_DISK
             || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        ok = fs_fail(error, FS_INVALID, ERROR_INVALID_DATA, "require regular file", path);
    else if ((((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow) > limit)
        ok = fs_fail(error, FS_INVALID, ERROR_FILE_TOO_LARGE, "regular file size limit", path);
    if (!ok) {
        CloseHandle(handle);
        return false;
    }
    int fd = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
    if (fd < 0) {
        fs_fail(error, FS_IO, errno, "regular file descriptor", path);
        CloseHandle(handle);
        return false;
    }
    FILE *file = _fdopen(fd, "rb");
    if (!file) {
        fs_fail(error, FS_IO, errno, "regular file stream", path);
        _close(fd);
        return false;
    }
    return fs_read_regular_stream(file, path, limit, data, size, error);
}

bool inspect_fs_path(const char *path, bool follow_links, fs_info_t *info, fs_error_t *error) {
    if (!info)
        return fs_fail(error, FS_INVALID, EINVAL, "stat output", path);
    if (!fs_valid(path, error))
        return false;
    HANDLE h = open_path(path,
                         FILE_READ_ATTRIBUTES,
                         follow_links,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (h == INVALID_HANDLE_VALUE)
        return fs_native_error(error, "open stat", path);
    BY_HANDLE_FILE_INFORMATION s;
    bool ok = GetFileInformationByHandle(h, &s) != 0;
    if (!ok)
        fs_native_error(error, "stat", path);
    else
        *info = (fs_info_t){.kind = s.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT ? FS_LINK
                                    : s.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY   ? FS_DIRECTORY
                                    : GetFileType(h) == FILE_TYPE_DISK                ? FS_FILE
                                                                                      : FS_OTHER,
                            .size = ((uint64_t)s.nFileSizeHigh << 32) | s.nFileSizeLow,
                            .device = s.dwVolumeSerialNumber,
                            .identity = ((uint64_t)s.nFileIndexHigh << 32) | s.nFileIndexLow,
                            .hard_links = s.nNumberOfLinks};
    if (!CloseHandle(h) && ok)
        ok = fs_native_error(error, "close stat", path);
    return ok;
}

bool create_fs_directory(const char *path, fs_error_t *error) {
    if (!fs_valid(path, error))
        return false;
    wchar_t *wide = fs_wide(path);
    if (!wide)
        return fs_native_error(error, "decode path", path);
    bool ok = CreateDirectoryW(wide, NULL) != 0;
    if (!ok)
        fs_native_error(error, "mkdir", path);
    FREE(wide);
    return ok;
}

char *create_fs_temporary(const char *prefix, bool directory, FILE **file, fs_error_t *error) {
    if (file)
        *file = NULL;
    if (!fs_valid(prefix, error) || (!directory && !file))
        return NULL;
    char *path = ALLOC(strlen(prefix) + 40);
    for (int attempt = 0; attempt < 128; attempt++) {
        unsigned char random[16];
        if (BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG)) {
            fs_fail(error, FS_IO, ERROR_GEN_FAILURE, "random", prefix);
            break;
        }
        strcpy(path, prefix);
        size_t n = strlen(path);
        path[n++] = '-';
        for (size_t i = 0; i < sizeof(random); i++) {
            sprintf(path + n, "%02x", random[i]);
            n += 2;
        }
        wchar_t *wide = fs_wide(path);
        if (!wide) {
            fs_native_error(error, "decode path", path);
            break;
        }
        bool ok = false;
        HANDLE h = INVALID_HANDLE_VALUE;
        if (directory)
            ok = CreateDirectoryW(wide, NULL) != 0;
        else {
            h = CreateFileW(wide,
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            NULL,
                            CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
            ok = h != INVALID_HANDLE_VALUE;
        }
        DWORD code = GetLastError();
        FREE(wide);
        if (ok) {
            if (directory)
                return path;
            int fd = _open_osfhandle((intptr_t)h, _O_RDWR | _O_BINARY | _O_NOINHERIT);
            if (fd >= 0) {
                *file = _fdopen(fd, "w+b");
                if (*file)
                    return path;
                _close(fd);
            } else
                CloseHandle(h);
            fs_fail(error, FS_IO, errno, "fdopen", path);
            remove_fs_entry(path, NULL);
            break;
        }
        SetLastError(code);
        if (code != ERROR_ALREADY_EXISTS && code != ERROR_FILE_EXISTS) {
            fs_native_error(error, "create temporary", path);
            break;
        }
        if (attempt == 127)
            fs_native_error(error, "create temporary", path);
    }
    FREE(path);
    return NULL;
}

/* NtCreateFile's RootDirectory anchors a single child name to its parent handle.
 * No path reconstructed from directory names is used to open or delete a child.
 */
static HANDLE open_child(HANDLE parent, wchar_t *name, DWORD access) {
    typedef NTSTATUS(NTAPI * create_fn)(PHANDLE,
                                        ACCESS_MASK,
                                        POBJECT_ATTRIBUTES,
                                        PIO_STATUS_BLOCK,
                                        PLARGE_INTEGER,
                                        ULONG,
                                        ULONG,
                                        ULONG,
                                        ULONG,
                                        PVOID,
                                        ULONG);
    typedef ULONG(WINAPI * error_fn)(NTSTATUS);
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    FARPROC create_address = GetProcAddress(module, "NtCreateFile");
    FARPROC error_address = GetProcAddress(module, "RtlNtStatusToDosError");
    create_fn create;
    error_fn convert;
    _Static_assert(sizeof(create) == sizeof(create_address), "Windows function pointers");
    _Static_assert(sizeof(convert) == sizeof(error_address), "Windows function pointers");
    memcpy(&create, &create_address, sizeof(create));
    memcpy(&convert, &error_address, sizeof(convert));
    if (!create || !convert) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return INVALID_HANDLE_VALUE;
    }
    UNICODE_STRING text = {.Length = (USHORT)(wcslen(name) * sizeof(wchar_t)), .Buffer = name};
    text.MaximumLength = text.Length;
    OBJECT_ATTRIBUTES attributes = {.Length = sizeof(attributes),
                                    .RootDirectory = parent,
                                    .ObjectName = &text};
    IO_STATUS_BLOCK io;
    HANDLE h;
    NTSTATUS status =
        create(&h,
               access | SYNCHRONIZE,
               &attributes,
               &io,
               NULL,
               0,
               FILE_SHARE_READ | FILE_SHARE_WRITE,
               1 /* FILE_OPEN */,
               0x00200000 /* FILE_OPEN_REPARSE_POINT */ | 0x20 /* FILE_SYNCHRONOUS_IO_NONALERT */,
               NULL,
               0);
    if (status < 0) {
        SetLastError(convert(status));
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

static bool mark_deleted(HANDLE h, const char *path, fs_error_t *error) {
    FILE_DISPOSITION_INFO info = {.DeleteFile = TRUE};
    return SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info))
           || fs_native_error(error, "delete handle", path);
}

static bool walk_directory(HANDLE h,
                           const char *path,
                           fs_entries_t *entries,
                           unsigned depth,
                           fs_error_t *error);

static bool remove_handle(HANDLE h, const char *path, unsigned depth, fs_error_t *error) {
    if (depth > 256)
        return fs_fail(error, FS_UNSUPPORTED, ERROR_DIRECTORY, "tree depth", path);
    BY_HANDLE_FILE_INFORMATION s;
    if (!GetFileInformationByHandle(h, &s))
        return fs_native_error(error, "stat tree", path);
    if ((s.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        && !(s.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        if (!walk_directory(h, path, NULL, depth + 1, error))
            return false;
    return mark_deleted(h, path, error);
}

static bool walk_directory(HANDLE h,
                           const char *path,
                           fs_entries_t *entries,
                           unsigned depth,
                           fs_error_t *error) {
    /* Heap storage is aligned for FILE_ID_BOTH_DIR_INFO. */
    void *buffer = ALLOC(65536);
    bool ok = true;
    for (;;) {
        if (!GetFileInformationByHandleEx(h, FileIdBothDirectoryInfo, buffer, 65536)) {
            if (GetLastError() != ERROR_NO_MORE_FILES)
                ok = fs_native_error(error, "enumerate directory", path);
            break;
        }
        FILE_ID_BOTH_DIR_INFO *entry = buffer;
        for (;;) {
            size_t n = entry->FileNameLength / sizeof(wchar_t);
            wchar_t *wide = ALLOC((n + 1) * sizeof(*wide));
            memcpy(wide, entry->FileName, entry->FileNameLength);
            wide[n] = 0;
            if (wcscmp(wide, L".") && wcscmp(wide, L"..")) {
                char *name = fs_utf8(wide);
                if (!name)
                    ok = fs_native_error(error, "encode directory name", path);
                else if (entries) {
                    char **names = ALLOC((entries->count + 1) * sizeof(*names));
                    if (entries->count)
                        memcpy(names, entries->names, entries->count * sizeof(*names));
                    FREE(entries->names);
                    entries->names = names;
                    names[entries->count++] = name;
                    name = NULL;
                } else {
                    char *child = join_fs_path(path, name, error);
                    HANDLE opened =
                        open_child(h, wide, DELETE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES);
                    if (opened == INVALID_HANDLE_VALUE)
                        ok = fs_native_error(error, "open tree child", child);
                    else {
                        BY_HANDLE_FILE_INFORMATION actual;
                        if (!GetFileInformationByHandle(opened, &actual))
                            ok = fs_native_error(error, "stat child", child);
                        else if ((((uint64_t)actual.nFileIndexHigh << 32) | actual.nFileIndexLow)
                                 != (uint64_t)entry->FileId.QuadPart)
                            ok = fs_fail(error,
                                         FS_CHANGED,
                                         ERROR_FILE_INVALID,
                                         "tree identity",
                                         child);
                        else
                            ok = remove_handle(opened, child, depth, error);
                        if (!CloseHandle(opened) && ok)
                            ok = fs_native_error(error, "close child", child);
                    }
                    FREE(child);
                }
                FREE(name);
            }
            FREE(wide);
            if (!ok || !entry->NextEntryOffset)
                break;
            entry = (FILE_ID_BOTH_DIR_INFO *)((char *)entry + entry->NextEntryOffset);
        }
        if (!ok)
            break;
    }
    FREE(buffer);
    return ok;
}

bool list_fs_directory(const char *path, fs_entries_t *entries, fs_error_t *error) {
    if (!entries)
        return fs_fail(error, FS_INVALID, EINVAL, "list output", path);
    if (!fs_valid(path, error))
        return false;
    *entries = (fs_entries_t){0};
    HANDLE h = open_path(path, FILE_LIST_DIRECTORY, true, FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (h == INVALID_HANDLE_VALUE)
        return fs_native_error(error, "open directory", path);
    bool ok = walk_directory(h, path, entries, 0, error);
    if (!CloseHandle(h) && ok)
        ok = fs_native_error(error, "close directory", path);
    if (!ok)
        clear_fs_entries(entries);
    return ok;
}

bool remove_fs_entry(const char *path, fs_error_t *error) {
    if (!fs_valid(path, error))
        return false;
    HANDLE h = open_path(path, DELETE, false, FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (h == INVALID_HANDLE_VALUE)
        return fs_native_error(error, "open remove", path);
    bool ok = mark_deleted(h, path, error);
    if (!CloseHandle(h) && ok)
        ok = fs_native_error(error, "close remove", path);
    return ok;
}

bool remove_fs_tree(const char *path, fs_error_t *error) {
    if (!fs_valid(path, error))
        return false;
    HANDLE h = open_path(path,
                         DELETE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
                         false,
                         FILE_SHARE_READ | FILE_SHARE_WRITE);
    if (h == INVALID_HANDLE_VALUE)
        return fs_native_error(error, "open tree", path);
    bool ok = remove_handle(h, path, 0, error);
    if (!CloseHandle(h) && ok)
        ok = fs_native_error(error, "close tree", path);
    return ok;
}

bool prepare_fs_replace(const char *prepared, const char *destination, fs_error_t *error) {
    fs_info_t temp, old;
    if (!inspect_fs_path(prepared, false, &temp, error))
        return false;
    if (temp.kind != FS_FILE || temp.hard_links != 1)
        return fs_fail(error, FS_INVALID, ERROR_INVALID_DATA, "prepared regular file", prepared);
    fs_error_t probe = {0};
    bool exists = inspect_fs_path(destination, false, &old, &probe);
    if (!exists) {
        bool absent = probe.status == FS_NOT_FOUND;
        if (!absent)
            fs_fail(error, probe.status, probe.native_code, probe.operation, probe.path);
        clear_fs_error(&probe);
        return absent;
    }
    if (old.kind == FS_LINK)
        return true;
    if (old.kind != FS_FILE)
        return fs_fail(error,
                       FS_INVALID,
                       ERROR_INVALID_DATA,
                       "destination regular file",
                       destination);
    HANDLE source =
        open_path(destination, READ_CONTROL | FILE_READ_ATTRIBUTES, false, FILE_SHARE_READ);
    if (source == INVALID_HANDLE_VALUE)
        return fs_native_error(error, "open permissions", destination);
    HANDLE target = open_path(prepared,
                              READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES,
                              false,
                              FILE_SHARE_READ);
    bool ok = target != INVALID_HANDLE_VALUE;
    if (!ok)
        fs_native_error(error, "open prepared permissions", prepared);
    BY_HANDLE_FILE_INFORMATION info;
    if (ok && (!GetFileInformationByHandle(target, &info) || info.nNumberOfLinks != 1))
        ok = fs_fail(error, FS_INVALID, ERROR_INVALID_DATA, "prepared unique file", prepared);
    DWORD flags = 0;
    if (ok
        && (!GetVolumeInformationByHandleW(source, NULL, 0, NULL, NULL, &flags, NULL, 0)
            || !(flags & FILE_PERSISTENT_ACLS)))
        ok = fs_fail(error,
                     FS_UNSUPPORTED,
                     ERROR_NOT_SUPPORTED,
                     "persistent ACL support",
                     destination);
    PSECURITY_DESCRIPTOR descriptor = NULL;
    PSID owner = NULL, group = NULL;
    PACL dacl = NULL;
    DWORD code = 0;
    if (ok) {
        code = GetSecurityInfo(source,
                               SE_FILE_OBJECT,
                               OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION
                                   | DACL_SECURITY_INFORMATION,
                               &owner,
                               &group,
                               &dacl,
                               NULL,
                               &descriptor);
        if (code)
            ok = fs_fail(error, FS_ACCESS, code, "read ACL", destination);
    }
    PSECURITY_DESCRIPTOR target_descriptor = NULL;
    PSID target_owner = NULL, target_group = NULL;
    if (ok) {
        code = GetSecurityInfo(target,
                               SE_FILE_OBJECT,
                               OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
                               &target_owner,
                               &target_group,
                               NULL,
                               NULL,
                               &target_descriptor);
        if (code)
            ok = fs_fail(error, FS_ACCESS, code, "read prepared owner", prepared);
    }
    if (ok) {
        /* Reassigning an unchanged owner/group can require restore privileges.
         * Only request ownership changes when they are actually necessary. */
        SECURITY_INFORMATION changes =
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION;
        if (!EqualSid(owner, target_owner))
            changes |= OWNER_SECURITY_INFORMATION;
        if (!EqualSid(group, target_group))
            changes |= GROUP_SECURITY_INFORMATION;
        HANDLE ownership = INVALID_HANDLE_VALUE;
        if (changes & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION)) {
            /* Reopen the same object, not its path: only real ownership changes
             * require WRITE_OWNER. Ordinary replacement must not require it. */
            ownership = ReOpenFile(target, WRITE_DAC | WRITE_OWNER, FILE_SHARE_READ, 0);
            if (ownership == INVALID_HANDLE_VALUE)
                ok = fs_native_error(error, "open prepared ownership", prepared);
        }
        if (ok) {
            code = SetSecurityInfo(ownership == INVALID_HANDLE_VALUE ? target : ownership,
                                   SE_FILE_OBJECT,
                                   changes,
                                   owner,
                                   group,
                                   dacl,
                                   NULL);
            if (code)
                ok = fs_fail(error, FS_ACCESS, code, "copy ACL", prepared);
        }
        if (ownership != INVALID_HANDLE_VALUE && !CloseHandle(ownership) && ok)
            ok = fs_native_error(error, "close prepared ownership", prepared);
    }
    if (target_descriptor)
        LocalFree(target_descriptor);
    if (descriptor)
        LocalFree(descriptor);
    if (target != INVALID_HANDLE_VALUE && !CloseHandle(target) && ok)
        ok = fs_native_error(error, "close prepared", prepared);
    if (!CloseHandle(source) && ok)
        ok = fs_native_error(error, "close permissions", destination);
    return ok;
}

bool rename_fs_path(const char *prepared, const char *destination, fs_error_t *error) {
    if (!fs_valid(prepared, error) || !fs_valid(destination, error))
        return false;
    wchar_t *a = fs_wide(prepared), *b = fs_wide(destination);
    bool ok = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    if (!ok)
        fs_native_error(error, "publish rename", destination);
    FREE(a);
    FREE(b);
    return ok;
}

bool fs_arguments(int *argc, char ***argv, fs_error_t *error) {
    if (!argc || !argv)
        return fs_fail(error, FS_INVALID, ERROR_INVALID_PARAMETER, "arguments", NULL);
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide)
        return fs_native_error(error, "command line", NULL);
    char **out = CALLOC(((size_t)count + 1) * sizeof(*out));
    for (int i = 0; i < count; i++) {
        out[i] = fs_utf8(wide[i]);
        if (!out[i]) {
            fs_native_error(error, "encode argument", NULL);
            free_fs_arguments(i, out);
            LocalFree(wide);
            return false;
        }
    }
    LocalFree(wide);
    *argc = count;
    *argv = out;
    return true;
}
#endif
