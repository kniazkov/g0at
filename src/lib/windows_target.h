/** @file windows_target.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Native backend API floor; include before any Windows system headers.
 */
#pragma once

#ifdef _WIN32
/* Restricted handle inheritance, BCrypt and safe DLL search require modern Windows APIs. */
#    if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#        undef _WIN32_WINNT
#        define _WIN32_WINNT 0x0602
#    endif
#    if !defined(WINVER) || WINVER < 0x0602
#        undef WINVER
#        define WINVER _WIN32_WINNT
#    endif
#endif
