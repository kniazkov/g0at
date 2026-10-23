/** @file test_file_system.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real filesystem errors, publication, Unicode and link isolation.
 */
#ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#endif
#include "test_file_system.h"

#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "lib/child_process.h"
#include "lib/file_system_internal.h"
#include "lib/os_thread.h"
#include "test_macro.h"

#include <errno.h>
#include <stdatomic.h>
#include <string.h>
#ifdef _WIN32
#    include <aclapi.h>
#    include <io.h>
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <linux/capability.h>
#    include <sys/stat.h>
#    include <sys/syscall.h>
#    include <sys/wait.h>
#    include <sys/xattr.h>
#    include <unistd.h>
#endif

static bool prepare_checked(const char *temporary, const char *destination) {
    fs_error_t error = {0};
    bool ok = prepare_fs_replace(temporary, destination, &error);
    if (!ok)
        fprintf(stderr,
                "prepare: %s, code %lu, path %s\n",
                error.operation,
                error.native_code,
                error.path);
    clear_fs_error(&error);
    return ok;
}

static bool content_is(const char *path, const char *expected) {
    size_t size;
    char *data = read_binary_file(path, 1024, &size);
    bool ok = data && size == strlen(expected) && !memcmp(data, expected, size);
    FREE(data);
    return ok;
}

static bool put(const char *path, const char *value) {
    FILE *file = FOPEN(path, "wb");
    if (!file)
        return false;
    bool ok = write_fs_file(file, value, strlen(value), path, NULL);
    return close_fs_file(file, path, NULL) && ok;
}

typedef struct {
    const char *prefix;
    char *paths[12];
    bool directory;
} temp_task_t;

static void *make_temporaries(void *data) {
    temp_task_t *task = data;
    for (int i = 0; i < 12; i++) {
        FILE *file;
        task->paths[i] = create_fs_temporary(task->prefix, task->directory, &file, NULL);
        if (!task->paths[i])
            return NULL;
        if (!task->directory && !close_fs_file(file, task->paths[i], NULL))
            return NULL;
    }
    return task;
}

static bool temporary_concurrency(const char *root) {
    char *prefix = join_fs_path(root, "временный-😀", NULL);
    temp_task_t tasks[4] = {0};
    os_thread_t *threads[4];
    for (int i = 0; i < 4; i++) {
        tasks[i].prefix = prefix;
        tasks[i].directory = i % 2;
        ASSERT(create_os_thread(&threads[i], make_temporaries, &tasks[i], 0) == OS_THREAD_OK);
    }
    for (int i = 0; i < 4; i++) {
        void *result;
        ASSERT(join_os_thread(threads[i], &result) == OS_THREAD_OK && result == &tasks[i]);
        ASSERT(destroy_os_thread(threads[i]) == OS_THREAD_OK);
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 12; j++) {
            for (int a = 0; a < 4; a++)
                for (int b = 0; b < 12; b++)
                    if (a != i || b != j)
                        ASSERT(strcmp(tasks[i].paths[j], tasks[a].paths[b]));
        }
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 12; j++) {
            ASSERT(remove_fs_entry(tasks[i].paths[j], NULL));
            FREE(tasks[i].paths[j]);
        }
    FREE(prefix);
    return true;
}

static bool publication(const char *root) {
    char *dest = join_fs_path(root, "результат-😀", NULL);
    char *hard = join_fs_path(root, "hard", NULL);
    ASSERT(put(dest, "old"));
#ifdef _WIN32
    wchar_t *wd = fs_wide(dest), *wh = fs_wide(hard);
    ASSERT(CreateHardLinkW(wh, wd, NULL));
    FREE(wd);
    FREE(wh);
#else
    ASSERT(!link(dest, hard));
    ASSERT(!chmod(dest, 0640));
#endif
    FILE *file;
    char *temp = create_fs_temporary(dest, false, &file, NULL);
    ASSERT(temp);
    ASSERT(write_fs_file(file, "new", 3, temp, NULL));
    ASSERT(close_fs_file(file, temp, NULL));
    ASSERT(prepare_checked(temp, dest));
    ASSERT(content_is(dest, "old"));
    ASSERT(content_is(temp, "new"));
    ASSERT(publish_fs_replace(temp, dest, NULL));
    ASSERT(content_is(dest, "new"));
    ASSERT(content_is(hard, "old"));
#ifndef _WIN32
    struct stat mode;
    ASSERT(!stat(dest, &mode) && (mode.st_mode & 0777) == 0640);
#endif
    FREE(temp);
    temp = create_fs_temporary(dest, false, &file, NULL);
    ASSERT(temp);
    ASSERT(close_fs_file(file, temp, NULL));
    fs_error_t error = {0};
    ASSERT(!prepare_fs_replace(temp, root, &error));
    ASSERT(error.operation && error.path && error.native_code);
    ASSERT(content_is(dest, "new"));
    clear_fs_error(&error);
    ASSERT(!publish_fs_replace(temp, root, &error));
    ASSERT(content_is(dest, "new"));
    clear_fs_error(&error);
    ASSERT(remove_fs_entry(temp, NULL));
    FREE(temp);
    ASSERT(!publish_fs_replace(temp = join_fs_path(root, "missing", NULL), dest, &error));
    ASSERT(content_is(dest, "new"));
    FREE(temp);
    clear_fs_error(&error);
    /* A failed stream write and a failed close are independently reported. */
    file = FOPEN(dest, "rb");
    ASSERT(file);
    ASSERT(!write_fs_file(file, "bad", 3, dest, &error));
    clear_fs_error(&error);
    fclose(file);
#ifndef _WIN32
    file = fopen("/dev/full", "wb");
    ASSERT(file);
    ASSERT(setvbuf(file, NULL, _IOFBF, 4096) == 0);
    ASSERT(write_fs_file(file, "bad", 3, "/dev/full", &error));
    ASSERT(!close_fs_file(file, "/dev/full", &error));
    clear_fs_error(&error);
#else
    file = FOPEN(hard, "wb");
    ASSERT(file);
    ASSERT(write_fs_file(file, "bad", 3, hard, &error));
    ASSERT(CloseHandle((HANDLE)_get_osfhandle(_fileno(file))));
    ASSERT(!close_fs_file(file, hard, &error));
    clear_fs_error(&error);
#endif
    ASSERT(content_is(dest, "new"));
    ASSERT(!write_binary_file(dest, NULL, 1));
    ASSERT(content_is(dest, "new"));
    FREE(dest);
    FREE(hard);
    return true;
}

/* Race a directory rename/link substitution against descriptor-relative cleanup.
 * The outcome of cleanup may be success or an explicit error; the external
 * target must survive every interleaving. No sleep controls the ordering.
 */
typedef struct {
    const char *tree, *parked, *outside;
    atomic_bool stop;
} swap_task_t;

static void *swap_tree(void *data) {
    swap_task_t *task = data;
#ifdef _WIN32
    wchar_t *tree = fs_wide(task->tree), *outside = fs_wide(task->outside);
#endif
    while (!atomic_load(&task->stop)) {
        if (publish_fs_replace(task->tree, task->parked, NULL)) {
#ifdef _WIN32
            if (CreateSymbolicLinkW(tree, outside, SYMBOLIC_LINK_FLAG_DIRECTORY | 2))
                remove_fs_entry(task->tree, NULL);
#else
            if (!symlink(task->outside, task->tree))
                unlink(task->tree);
#endif
            publish_fs_replace(task->parked, task->tree, NULL);
        }
    }
#ifdef _WIN32
    FREE(tree);
    FREE(outside);
#endif
    return data;
}

static bool substitution(const char *root) {
    char *outside = join_fs_path(root, "race-outside", NULL);
    char *sentinel = join_fs_path(outside, "sentinel", NULL);
    char *tree = join_fs_path(root, "race-tree", NULL);
    char *parked = join_fs_path(root, "race-parked", NULL);
    ASSERT(create_fs_directory(outside, NULL));
    ASSERT(put(sentinel, "untouched"));
    for (int attempt = 0; attempt < 40; attempt++) {
        ASSERT(create_fs_directory(tree, NULL));
        for (int i = 0; i < 8; i++) {
            char name[20];
            snprintf(name, sizeof(name), "child-%d", i);
            char *child = join_fs_path(tree, name, NULL);
            ASSERT(put(child, "owned"));
            FREE(child);
        }
        swap_task_t task = {.tree = tree, .parked = parked, .outside = outside};
        atomic_init(&task.stop, false);
        os_thread_t *thread;
        ASSERT(create_os_thread(&thread, swap_tree, &task, 0) == OS_THREAD_OK);
        fs_error_t error = {0};
        bool removed = remove_fs_tree(tree, &error);
        ASSERT(removed || (error.status != FS_OK && error.operation));
        clear_fs_error(&error);
        atomic_store(&task.stop, true);
        ASSERT(join_os_thread(thread, NULL) == OS_THREAD_OK);
        ASSERT(destroy_os_thread(thread) == OS_THREAD_OK);
        ASSERT(content_is(sentinel, "untouched"));
        fs_info_t info;
        if (inspect_fs_path(tree, false, &info, NULL))
            ASSERT(remove_fs_tree(tree, NULL));
        if (inspect_fs_path(parked, false, &info, NULL))
            ASSERT(remove_fs_tree(parked, NULL));
    }
    FREE(outside);
    FREE(sentinel);
    FREE(tree);
    FREE(parked);
    return true;
}
#ifndef _WIN32
static bool linux_contract(const char *root) {
    char *outside = join_fs_path(root, "outside", NULL);
    ASSERT(create_fs_directory(outside, NULL));
    char *sentinel = join_fs_path(outside, "sentinel", NULL);
    ASSERT(put(sentinel, "safe"));
    char *tree = join_fs_path(root, "tree", NULL);
    ASSERT(create_fs_directory(tree, NULL));
    char *linkpath = join_fs_path(tree, "link", NULL);
    ASSERT(!symlink(outside, linkpath));
    fs_info_t info;
    ASSERT(inspect_fs_path(linkpath, false, &info, NULL) && info.kind == FS_LINK);
    ASSERT(inspect_fs_path(linkpath, true, &info, NULL) && info.kind == FS_DIRECTORY);
    ASSERT(remove_fs_tree(tree, NULL));
    ASSERT(content_is(sentinel, "safe"));
    /* Invalid-byte names must fail enumeration and deletion, never silently disappear. */
    ASSERT(create_fs_directory(tree, NULL));
    char invalid[4096];
    snprintf(invalid, sizeof(invalid), "%s/\xff", tree);
    int fd = open(invalid, O_CREAT | O_WRONLY, 0600);
    ASSERT(fd >= 0);
    close(fd);
    fs_error_t error = {0};
    fs_entries_t entries;
    ASSERT(!list_fs_directory(tree, &entries, &error) && error.status == FS_INVALID);
    clear_fs_error(&error);
    ASSERT(!remove_fs_tree(tree, &error) && error.status == FS_INVALID);
    clear_fs_error(&error);
    ASSERT(!unlink(invalid));
    ASSERT(remove_fs_tree(tree, NULL));
    /* Extended POSIX ACL with a named user, encoded in Linux's documented xattr ABI. */
    uint32_t acl[] = {2,
                      0x00060001,
                      0xffffffff,
                      0x00040002,
                      0,
                      0x00040004,
                      0xffffffff,
                      0x00040010,
                      0xffffffff,
                      0x00000020,
                      0xffffffff};
    ASSERT(!setxattr(sentinel, "system.posix_acl_access", acl, sizeof(acl), 0));
    FILE *file;
    char *temp = create_fs_temporary(sentinel, false, &file, NULL);
    ASSERT(temp);
    ASSERT(close_fs_file(file, temp, NULL));
    ASSERT(prepare_fs_replace(temp, sentinel, &error));
    uint32_t copied[32];
    ASSERT(getxattr(temp, "system.posix_acl_access", copied, sizeof(copied)) == sizeof(acl));
    ASSERT(!memcmp(acl, copied, sizeof(acl)));
    ASSERT(remove_fs_entry(temp, NULL));
    FREE(temp);
    /* New files obey umask and the parent's default ACL rather than replacement rights. */
    mode_t saved_mask = umask(0027);
    temp = create_fs_temporary(sentinel, false, &file, NULL);
    umask(saved_mask);
    ASSERT(temp && close_fs_file(file, temp, NULL));
    struct stat created;
    ASSERT(!stat(temp, &created) && (created.st_mode & 0777) == 0640);
    char *absent = join_fs_path(root, "new-destination", NULL);
    ASSERT(prepare_fs_replace(temp, absent, NULL));
    ASSERT(!stat(temp, &created) && (created.st_mode & 0777) == 0640);
    ASSERT(remove_fs_entry(temp, NULL));
    FREE(temp);
    FREE(absent);
    ASSERT(!setxattr(outside, "system.posix_acl_default", acl, sizeof(acl), 0));
    temp = create_fs_temporary(sentinel, false, &file, NULL);
    ASSERT(temp && close_fs_file(file, temp, NULL));
    ASSERT(getxattr(temp, "system.posix_acl_access", copied, sizeof(copied)) == sizeof(acl));
    ASSERT(!memcmp(acl, copied, sizeof(acl)));
    ASSERT(remove_fs_entry(temp, NULL));
    FREE(temp);
    /* Real access denial, also when the suite itself runs as root. */
    ASSERT(!chmod(root, 0755));
    ASSERT(!chmod(outside, 0000));
    pid_t pid = fork();
    ASSERT(pid >= 0);
    if (!pid) {
        if (!geteuid()) {
            struct __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
            struct __user_cap_data_struct caps[2];
            if (syscall(SYS_capget, &header, caps))
                _exit(2);
            caps[0].effective &= ~((1U << CAP_DAC_OVERRIDE) | (1U << CAP_DAC_READ_SEARCH));
            if (syscall(SYS_capset, &header, caps))
                _exit(2);
        }
        fs_info_t denied;
        fs_error_t e = {0};
        bool failed = !inspect_fs_path(sentinel, true, &denied, &e) && e.status == FS_ACCESS;
        char *prepared = join_fs_path(root, "permission-failure", NULL);
        FILE *f = FOPEN(prepared, "wb");
        if (f) {
            fclose(f);
            failed &= !prepare_fs_replace(prepared, sentinel, &e) && e.status == FS_ACCESS;
            remove_fs_entry(prepared, NULL);
        } else
            failed = false;
        _exit(failed ? 0 : 1);
    }
    int status;
    ASSERT(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status));
    ASSERT(!chmod(outside, 0700));
    FREE(outside);
    FREE(sentinel);
    FREE(tree);
    FREE(linkpath);
    clear_fs_error(&error);
    return true;
}
#else
static bool windows_contract(const char *root) {
    char *outside = join_fs_path(root, "outside", NULL);
    ASSERT(create_fs_directory(outside, NULL));
    char *sentinel = join_fs_path(outside, "sentinel", NULL);
    ASSERT(put(sentinel, "safe"));
    char *tree = join_fs_path(root, "tree", NULL);
    ASSERT(create_fs_directory(tree, NULL));
    char *linkpath = join_fs_path(tree, "link", NULL);
    wchar_t *wl = fs_wide(linkpath), *wo = fs_wide(outside);
    ASSERT(CreateSymbolicLinkW(wl, wo, SYMBOLIC_LINK_FLAG_DIRECTORY | 2)
           || CreateSymbolicLinkW(wl, wo, SYMBOLIC_LINK_FLAG_DIRECTORY));
    FREE(wl);
    FREE(wo);
    fs_info_t info;
    ASSERT(inspect_fs_path(linkpath, false, &info, NULL) && info.kind == FS_LINK);
    ASSERT(inspect_fs_path(linkpath, true, &info, NULL) && info.kind == FS_DIRECTORY);
    ASSERT(remove_fs_tree(tree, NULL));
    ASSERT(content_is(sentinel, "safe"));
    ASSERT(create_fs_directory(tree, NULL));
    wchar_t *badbase = fs_wide(tree);
    size_t badlen = wcslen(badbase);
    wchar_t *bad = ALLOC((badlen + 4) * sizeof(*bad));
    memcpy(bad, badbase, badlen * sizeof(*bad));
    bad[badlen] = L'/';
    bad[badlen + 1] = 0xd800;
    bad[badlen + 2] = 0;
    HANDLE invalid = CreateFileW(bad, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    ASSERT(invalid != INVALID_HANDLE_VALUE);
    ASSERT(CloseHandle(invalid));
    fs_error_t encoding = {0};
    fs_entries_t entries;
    ASSERT(!list_fs_directory(tree, &entries, &encoding) && encoding.status == FS_INVALID);
    clear_fs_error(&encoding);
    ASSERT(!remove_fs_tree(tree, &encoding) && encoding.status == FS_INVALID);
    clear_fs_error(&encoding);
    ASSERT(DeleteFileW(bad));
    FREE(bad);
    FREE(badbase);
    ASSERT(remove_fs_tree(tree, NULL));
    /* Copy a protected DACL with a distinct read-only Everyone entry. */
    wchar_t *ws = fs_wide(sentinel);
    PSID everyone = NULL;
    SID_IDENTIFIER_AUTHORITY world = SECURITY_WORLD_SID_AUTHORITY;
    ASSERT(AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone));
    EXPLICIT_ACCESSW access = {0};
    access.grfAccessPermissions = GENERIC_READ;
    access.grfAccessMode = GRANT_ACCESS;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.ptstrName = everyone;
    PACL original = NULL, acl = NULL;
    PSECURITY_DESCRIPTOR descriptor = NULL;
    ASSERT(!GetNamedSecurityInfoW(ws,
                                  SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION,
                                  NULL,
                                  NULL,
                                  &original,
                                  NULL,
                                  &descriptor));
    ASSERT(!SetEntriesInAclW(1, &access, original, &acl));
    ASSERT(!SetNamedSecurityInfoW(ws,
                                  SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                  NULL,
                                  NULL,
                                  acl,
                                  NULL));
    FILE *file;
    char *temp = create_fs_temporary(sentinel, false, &file, NULL);
    ASSERT(temp);
    ASSERT(close_fs_file(file, temp, NULL));
    ASSERT(prepare_checked(temp, sentinel));
    wchar_t *wt = fs_wide(temp);
    PACL copied = NULL;
    PSECURITY_DESCRIPTOR copy = NULL;
    ASSERT(!GetNamedSecurityInfoW(wt,
                                  SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION,
                                  NULL,
                                  NULL,
                                  &copied,
                                  NULL,
                                  &copy));
    ASSERT(copied && copied->AclSize == acl->AclSize && !memcmp(copied, acl, acl->AclSize));
    LocalFree(copy);
    LocalFree(descriptor);
    LocalFree(acl);
    FreeSid(everyone);
    FREE(ws);
    FREE(wt);
    ASSERT(remove_fs_entry(temp, NULL));
    FREE(temp);
    /* Sharing refusal must preserve the old result. */
    ws = fs_wide(sentinel);
    HANDLE held = CreateFileW(ws, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    FREE(ws);
    ASSERT(held != INVALID_HANDLE_VALUE);
    temp = create_fs_temporary(sentinel, false, &file, NULL);
    ASSERT(temp);
    ASSERT(close_fs_file(file, temp, NULL));
    fs_error_t error = {0};
    ASSERT(!prepare_fs_replace(temp, sentinel, &error));
    clear_fs_error(&error);
    ASSERT(!publish_fs_replace(temp, sentinel, &error));
    clear_fs_error(&error);
    CloseHandle(held);
    ASSERT(content_is(sentinel, "safe"));
    ASSERT(remove_fs_entry(temp, NULL));
    FREE(temp);
    FREE(outside);
    FREE(sentinel);
    FREE(tree);
    FREE(linkpath);
    return true;
}
#endif
bool run_file_system_tests(void) {
    size_t memory_before = get_allocated_memory_size();
    fs_error_t error = {0};
    ASSERT(!validate_fs_path("a\0b", 3, &error) && error.status == FS_INVALID);
    clear_fs_error(&error);
    ASSERT(!validate_fs_path("\xc0\xaf", 2, &error) && error.status == FS_INVALID);
    clear_fs_error(&error);
    char *base = temporary_fs_directory(&error);
    ASSERT(base);
    char *prefix = join_fs_path(base, "goat-файлы-😀", &error);
    ASSERT(prefix);
    char *root = create_fs_temporary(prefix, true, NULL, &error);
    ASSERT(root);
    fs_info_t info;
    ASSERT(inspect_fs_path(root, false, &info, &error) && info.kind == FS_DIRECTORY);
    ASSERT(!create_fs_directory(root, &error) && error.status == FS_EXISTS);
    clear_fs_error(&error);
    char *missing = join_fs_path(root, "missing", NULL);
    ASSERT(!inspect_fs_path(missing, true, &info, &error) && error.status == FS_NOT_FOUND);
    clear_fs_error(&error);
    FREE(missing);
    ASSERT(temporary_concurrency(root));
    ASSERT(substitution(root));
    ASSERT(publication(root));
#ifdef _WIN32
    ASSERT(windows_contract(root));
#else
    ASSERT(linux_contract(root));
#endif
    fs_entries_t entries;
    ASSERT(list_fs_directory(root, &entries, &error) && entries.count >= 3);
    clear_fs_entries(&entries);
    ASSERT(remove_fs_tree(root, &error));
    ASSERT(!inspect_fs_path(root, false, &info, &error) && error.status == FS_NOT_FOUND);
    clear_fs_error(&error);
    FREE(root);
    FREE(prefix);
    FREE(base);
    ASSERT(get_allocated_memory_size() == memory_before);
    return true;
}

bool test_file_system(void) {
    const char *args[] = {"unit_testing", "--file-system-tests", NULL};
    child_options_t options = {.arguments = args,
                               .empty_stdin = true,
                               .capture_output = true,
                               .deadline = child_deadline_after(20000)};
    child_process_t *child = start_child_process(&options);
    ASSERT(child);
    const child_result_t *result = wait_child_process(child);
    bool ok = result->status == CHILD_EXITED && !result->exit_code && !result->system_error
              && !result->cleanup_error;
    if (!ok) {
        fprintf(stderr,
                "filesystem fixture status=%d exit=%lld error=%lu cleanup=%lu\n",
                result->status,
                (long long)result->exit_code,
                result->system_error,
                result->cleanup_error);
        fwrite(result->out.data, 1, result->out.size, stderr);
        fwrite(result->err.data, 1, result->err.size, stderr);
    }
    destroy_child_process(child);
    return ok;
}
