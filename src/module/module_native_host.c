/** @file module_native_host.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative capabilities of the executing process, without probing a module.
 */
#include "module_native.h"

#include <limits.h>
#include <string.h>
#if defined(__i386__) || defined(__x86_64__)
#    include <cpuid.h>
#endif
#ifdef _WIN32
#    include <windows.h>
#elif defined(__linux__)
#    include <sys/utsname.h>
#    ifdef __GLIBC__
#        include <gnu/libc-version.h>
#    endif
#    ifdef __aarch64__
#        include <asm/hwcap.h>
#        include <sys/auxv.h>
#    endif
#endif

#ifdef __linux__
/* Kernel releases may end in a packaging suffix. Parse only numeric components;
 * the suffix cannot raise the available version. Never truncate an overflow. */
static bool parse_version(const char *text, goat_native_version *out) {
    goat_native_version version = {0};
    for (size_t i = 0; i < 4; i++) {
        if (*text < '0' || *text > '9')
            return false;
        uint32_t value = 0;
        do {
            unsigned digit = (unsigned)(*text++ - '0');
            if (value > (UINT32_MAX - digit) / 10)
                return false;
            value = value * 10 + digit;
        } while (*text >= '0' && *text <= '9');
        version.components[i] = value;
        if (*text != '.' || text[1] < '0' || text[1] > '9')
            break;
        if (i == 3)
            return false;
        text++;
    }
    version.known = true;
    *out = version;
    return true;
}
#endif

#if defined(__i386__) || defined(__x86_64__)
static uint64_t x86_features(void) {
    unsigned a, b, c, d;
    if (!__get_cpuid(1, &a, &b, &c, &d))
        return 0;
    uint64_t features = 0;
    unsigned family = (a >> 8) & 15;
    if (family == 15)
        family += (a >> 20) & 255;
    if (family >= 6 && (d & bit_CMOV) && (d & bit_CMPXCHG8B))
        features |= GOAT_CPU_I686;
    /* All supported x86 operating systems save SSE state. On Windows explicitly
     * check its processor-feature contract as well as the hardware capability. */
    bool sse_state = true;
#    ifdef _WIN32
    sse_state = IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE) != 0;
#    endif
    if (sse_state && (d & bit_SSE2)) {
        features |= GOAT_CPU_SSE2;
        if (c & bit_SSE3)
            features |= GOAT_CPU_SSE3;
        if (c & bit_SSSE3)
            features |= GOAT_CPU_SSSE3;
        if (c & bit_SSE4_1)
            features |= GOAT_CPU_SSE4_1;
        if (c & bit_SSE4_2)
            features |= GOAT_CPU_SSE4_2;
        if (c & bit_AES)
            features |= GOAT_CPU_AES;
    }
    uint64_t xcr0 = 0;
    if ((c & bit_XSAVE) && (c & bit_OSXSAVE)) {
        unsigned low, high;
        /* XGETBV is legal only after the CPUID OSXSAVE check above. Inline
         * assembly avoids requiring -mxsave (or an elevated CPU baseline). */
        __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
        xcr0 = ((uint64_t)high << 32) | low;
    }
    if ((features & GOAT_CPU_SSE2) && (c & bit_AVX) && (xcr0 & 6) == 6) {
        features |= GOAT_CPU_AVX;
        if (c & bit_FMA)
            features |= GOAT_CPU_FMA;
    }
    if (__get_cpuid_max(0, NULL) >= 7) {
        __cpuid_count(7, 0, a, b, c, d);
        if ((features & GOAT_CPU_AVX) && (b & bit_AVX2))
            features |= GOAT_CPU_AVX2;
        if (b & bit_BMI)
            features |= GOAT_CPU_BMI1;
        if (b & bit_BMI2)
            features |= GOAT_CPU_BMI2;
        if ((features & GOAT_CPU_AVX) && (b & bit_AVX512F) && (xcr0 & 0xe6) == 0xe6)
            features |= GOAT_CPU_AVX512F;
        if ((features & GOAT_CPU_SSE2) && (b & bit_SHA))
            features |= GOAT_CPU_SHA1 | GOAT_CPU_SHA2;
    }
    return features;
}
#endif

#if defined(__linux__) && defined(__aarch64__)
static uint64_t arm_features(void) {
    unsigned long caps = getauxval(AT_HWCAP);
    uint64_t features = 0;
    if (caps & HWCAP_FP)
        features |= GOAT_CPU_FP;
    if (caps & HWCAP_ASIMD)
        features |= GOAT_CPU_NEON;
    if (caps & HWCAP_AES)
        features |= GOAT_CPU_AES;
    if (caps & HWCAP_SHA1)
        features |= GOAT_CPU_SHA1;
    if (caps & HWCAP_SHA2)
        features |= GOAT_CPU_SHA2;
    if (caps & HWCAP_CRC32)
        features |= GOAT_CPU_CRC32;
    if (caps & HWCAP_ATOMICS)
        features |= GOAT_CPU_LSE;
#    ifdef HWCAP_SVE
    if (caps & HWCAP_SVE)
        features |= GOAT_CPU_SVE;
#    endif
#    if defined(AT_HWCAP2) && defined(HWCAP2_SVE2)
    if ((features & GOAT_CPU_SVE) && (getauxval(AT_HWCAP2) & HWCAP2_SVE2))
        features |= GOAT_CPU_SVE2;
#    endif
    return features;
}
#endif

#ifdef _WIN32
static void windows_version(goat_native_version *out) {
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    if (!module)
        return;
    typedef LONG(WINAPI * get_version_fn)(OSVERSIONINFOW *);
    get_version_fn get_version = NULL;
    FARPROC address = GetProcAddress(module, "RtlGetVersion");
    _Static_assert(sizeof(address) == sizeof(get_version), "Windows function pointers");
    memcpy(&get_version, &address, sizeof(get_version));
    if (!get_version)
        return;
    OSVERSIONINFOW version = {0};
    version.dwOSVersionInfoSize = sizeof(version);
    if (get_version(&version) != 0)
        return;
    out->components[0] = version.dwMajorVersion;
    out->components[1] = version.dwMinorVersion;
    out->components[2] = version.dwBuildNumber;
    /* RtlGetVersion supplies no update revision: zero is a safe lower bound,
     * never a claim that a later servicing revision is available. */
    out->components[3] = 0;
    out->known = true;
}

static void windows_runtime_version(const wchar_t *name, goat_native_version *out) {
    /* Read the resource of the CRT already mapped into this process, not an
     * unrelated DLL on PATH or a replacement file on disk. No library is loaded. */
    HMODULE module = GetModuleHandleW(name);
    if (!module)
        return;
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));
    if (!resource)
        return;
    DWORD size = SizeofResource(module, resource);
    const unsigned char *bytes = LockResource(LoadResource(module, resource));
    static const wchar_t key[] = L"VS_VERSION_INFO";
    size_t offset = (6 + sizeof(key) + 3) & ~(size_t)3;
    if (!bytes || size < offset + sizeof(VS_FIXEDFILEINFO))
        return;
    WORD length, value_length, type;
    memcpy(&length, bytes, 2);
    memcpy(&value_length, bytes + 2, 2);
    memcpy(&type, bytes + 4, 2);
    if (length > size || length < offset + sizeof(VS_FIXEDFILEINFO)
        || value_length < sizeof(VS_FIXEDFILEINFO) || type != 0
        || memcmp(bytes + 6, key, sizeof(key)))
        return;
    VS_FIXEDFILEINFO fixed;
    memcpy(&fixed, bytes + offset, sizeof(fixed));
    if (fixed.dwSignature != 0xfeef04bd)
        return;
    out->components[0] = HIWORD(fixed.dwFileVersionMS);
    out->components[1] = LOWORD(fixed.dwFileVersionMS);
    out->components[2] = HIWORD(fixed.dwFileVersionLS);
    out->components[3] = LOWORD(fixed.dwFileVersionLS);
    out->known = true;
}
#endif

bool goat_native_environment_detect(goat_native_environment *out) {
    if (!out)
        return false;
    *out = (goat_native_environment){0};
    const uint32_t endian = 1;
    out->little_endian = *(const unsigned char *)&endian == 1;
    out->bits = (unsigned)(sizeof(void *) * CHAR_BIT);
#if defined(__x86_64__)
    out->architecture = GOAT_ARCH_X86_64;
#elif defined(__i386__)
    out->architecture = GOAT_ARCH_X86;
#elif defined(__aarch64__)
    out->architecture = GOAT_ARCH_AARCH64;
#endif
#if defined(__i386__) || defined(__x86_64__)
    out->cpu_features = x86_features();
#elif defined(__linux__) && defined(__aarch64__)
    out->cpu_features = arm_features();
#endif
#ifdef _WIN32
    out->os = GOAT_OS_WINDOWS;
    windows_version(&out->os_version);
#    if defined(_UCRT)
    out->runtime = GOAT_RUNTIME_UCRT;
    windows_runtime_version(L"ucrtbase.dll", &out->runtime_version);
#    elif defined(__MSVCRT__)
    out->runtime = GOAT_RUNTIME_MSVCRT;
    windows_runtime_version(L"msvcrt.dll", &out->runtime_version);
#    endif
#elif defined(__linux__)
    out->os = GOAT_OS_LINUX;
    struct utsname system;
    if (!uname(&system))
        parse_version(system.release, &out->os_version);
#    ifdef __GLIBC__
    out->runtime = GOAT_RUNTIME_GLIBC;
    parse_version(gnu_get_libc_version(), &out->runtime_version);
#    endif
    /* No portable musl identification/version API exists. Its native backend is
     * deferred; do not infer musl merely from the absence of __GLIBC__. */
#endif
    return out->little_endian && out->os != GOAT_OS_UNKNOWN
           && out->architecture != GOAT_ARCH_UNKNOWN && out->runtime != GOAT_RUNTIME_UNKNOWN;
}
