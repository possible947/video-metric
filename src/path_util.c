/*====================================================================*/
/*  FILE: src/path_util.c                                            */
/*====================================================================*/
/**
 * @brief Retrieve the directory of the running executable and
 *        provide shell-safe path escaping and video duration query.
 *
 * On POSIX systems this uses /proc/self/exe symlink.
 * On Windows it uses GetModuleFileNameA() and PathRemoveFileSpecA().
 */

#include "path_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef _WIN32
#   include <windows.h>
#   include <Shlwapi.h>
#   pragma comment(lib, "Shlwapi.lib")
#elif defined(__APPLE__)
#   include <mach-o/dyld.h>
#   include <stdint.h>
#else
#   include <unistd.h>
#endif

/* ------------------------------------------------------------------ */
/*  escape_path                                                       */
/* ------------------------------------------------------------------ */
void escape_path(const char *src, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }

    size_t out = 0;
    /* Opening double-quote */
    if (out + 1 < dst_size) dst[out++] = '"';

    for (const char *p = src; *p && out + 2 < dst_size; p++) {
        /* Characters that must be backslash-escaped inside double quotes */
        if (*p == '"' || *p == '\\' || *p == '$' || *p == '`' || *p == '!') {
            dst[out++] = '\\';
        }
        dst[out++] = *p;
    }

    /* Closing double-quote + NUL */
    if (out + 1 < dst_size) dst[out++] = '"';
    dst[out] = '\0';
}

/* ------------------------------------------------------------------ */
/*  get_video_duration                                                */
/* ------------------------------------------------------------------ */
double get_video_duration(const char *video_path)
{
    if (!video_path) return -1.0;

    char cmd[4096];
    int n = snprintf(cmd, sizeof(cmd),
        "ffprobe -v error -select_streams v:0 "
        "-show_entries format=duration "
        "-of default=noprint_wrappers=1:nokey=1 "
        "\"%s\" 2>/dev/null",
        video_path);
    if (n < 0 || (size_t)n >= sizeof(cmd)) return -1.0;

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1.0;

    double duration = -1.0;
    if (fscanf(fp, "%lf", &duration) != 1)
        duration = -1.0;

    pclose(fp);
    return duration;
}

/* ------------------------------------------------------------------ */
/*  get_executable_dir                                                */
/* ------------------------------------------------------------------ */

int get_executable_dir(char *buf, size_t buflen)
{
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, buf, (DWORD)buflen);
    if (len == 0 || len == buflen) return -1;   /* buffer too small */
    PathRemoveFileSpecA(buf);                    /* drop the file name */
    return 0;
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)buflen;
    if (_NSGetExecutablePath(buf, &size) != 0) return -1;
    /* resolve symlinks */
    char resolved[PATH_MAX];
    if (!realpath(buf, resolved)) return -1;
    strncpy(buf, resolved, buflen - 1);
    buf[buflen - 1] = '\0';
    char *p = strrchr(buf, '/');
    if (!p) return -1;
    *p = '\0';
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
