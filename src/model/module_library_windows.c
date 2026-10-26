/** @file module_library_windows.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Windows mapped-image checks isolated from VM opcode names.
 */
#include "lib/windows_target.h"
#include "module_library_platform.h"

#ifdef _WIN32
#    include <stdint.h>
#    include <string.h>
#    include <windows.h>

bool module_windows_image_range(void *handle, const void *pointer, size_t length, bool code) {
    uintptr_t first = (uintptr_t)pointer;
    if (!pointer || !length || length > UINTPTR_MAX - first)
        return false;
    uintptr_t last = first + length;
    while (first < last) {
        MEMORY_BASIC_INFORMATION information;
        if (!VirtualQuery((void *)first, &information, sizeof(information))
            || information.AllocationBase != handle || information.State != MEM_COMMIT
            || information.Type != MEM_IMAGE || (information.Protect & PAGE_GUARD))
            return false;
        DWORD protection = information.Protect & 0xff;
        if (code ? protection != PAGE_EXECUTE_READ
                 : protection != PAGE_READONLY && protection != PAGE_EXECUTE_READ)
            return false;
        uintptr_t begin = (uintptr_t)information.BaseAddress;
        if (information.RegionSize > UINTPTR_MAX - begin || begin + information.RegionSize <= first)
            return false;
        first = begin + information.RegionSize;
    }
    return true;
}

void *module_windows_symbol_address(void *handle, const char *symbol) {
    FARPROC function = GetProcAddress((HMODULE)handle, symbol);
    void *address = NULL;
    _Static_assert(sizeof(function) == sizeof(address), "Windows symbol pointer representation");
    memcpy(&address, &function, sizeof(address));
    return address;
}
#endif
