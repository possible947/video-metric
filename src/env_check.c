/*====================================================================*/
/*  FILE: src/env_check.c                                            */
/*====================================================================*/
/**
 * @brief Verify that required runtime components are available.
 *
 * On POSIX systems we simply run "ffmpeg -version" to see if ffmpeg is in PATH.
 * On Windows we use GetModuleHandleA() to test for the presence of ffmpeg.exe.
 */

#include "env_check.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#   include <windows.h>
#else
#   include <unistd.h>
#endif

int check_environment(char *status_buf, size_t buflen)
{
    /* 1. Check that ffmpeg is available */
#if defined(_WIN32)
    HMODULE h = GetModuleHandleA("ffmpeg.exe");
    if (!h) {
        snprintf(status_buf, buflen,
                 "Error: FFmpeg not found in PATH.");
        return -1;
    }
#else
    int rc = system("ffmpeg -version > /dev/null 2>&1");
    if (rc != 0) {
        snprintf(status_buf, buflen,
                 "Error: FFmpeg not found in PATH.");
        return -1;
    }
#endif

    /* Additional checks can be added here */

    snprintf(status_buf, buflen,
             "All required runtime components are present.");
    return 0;
}
