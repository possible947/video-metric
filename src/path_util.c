/*====================================================================*/
/*  FILE: src/path_util.c                                            */
/*====================================================================*/
/**
 * @brief Retrieve the directory of the running executable.
 *
 * On POSIX systems this uses /proc/self/exe symlink.
 * On Windows it uses GetModuleFileNameA() and PathRemoveFileSpecA().
 */

#include "path_util.h"

#ifdef _WIN32
#   include <windows.h>
#   include <Shlwapi.h>
#   pragma comment(lib, "Shlwapi.lib")
#else
#   include <unistd.h>
#endif

#include <string.h>

int get_executable_dir(char *buf, size_t buflen)
{
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)buflen);
    if (len == 0 || len == buflen) return -1;   /* buffer too small */
    PathRemoveFileSpecA(buf);                    /* drop the file name */
    return 0;
#else
    ssize_t len = readlink("/proc/self/exe", buf, buflen - 1);
    if (len < 0) return -1;
    buf[len] = '\0';
    /* remove trailing executable name */
    char *p = strrchr(buf, '/');
    if (!p) return -1;
    *p = '\0';
    return 0;
#endif
}
