/*====================================================================*/
/*  FILE: src/path_util.c                                           */
/*====================================================================*/
/**
 * @brief  Utility for escaping file paths for safe shell insertion.
 *
 * The function `escape_path()` transforms an arbitrary UTF‑8 string into a
 * shell‑safe representation that can be embedded in a double‑quoted
 * string.  It follows the POSIX rules described in the task statement:
 *
 *   • Double quotes (") are escaped as `\"`.
 *   • Backslashes (\) are escaped as `\\`.
 *   • Control characters (ASCII < 0x20 or 0x7F) are replaced with their
 *     octal escape sequence `\NNN`.
 *   • All other bytes (including UTF‑8 multi‑byte sequences) are copied
 *     unchanged.
 *
 * The output is always null‑terminated, but will be truncated if the
 * destination buffer is too small.  The function never writes past
 * `dst_size - 1` bytes, leaving the last byte as a terminating NUL.
 *
 * @param src       Input string; may be NULL or empty.
 * @param dst       Destination buffer.
 * @param dst_size  Size of the destination buffer in bytes.
 */
#include <stddef.h>   /* size_t */
#include <string.h>  /* strcpy, strlen */
#include <stdlib.h>  /* malloc, free */
#include <stdio.h>   /* popen, pclose */
#include <ctype.h>   /* isdigit */

/* ------------------------------------------------------------------ */
/*  Escape a path for shell insertion                                */
/* ------------------------------------------------------------------ */
void escape_path(const char *src, char *dst, size_t dst_size)
{
    if (!dst || dst_size == 0) {
        /* Nothing to write */
        return;
    }

    size_t pos = 0;          /* Current write position in dst */

    /* Start with the opening double quote */
    if (pos + 1 < dst_size) {
        dst[pos++] = '"';
    } else {
        /* No room even for the opening quote – just NUL‑terminate */
        dst[0] = '\0';
        return;
    }

    /* Process each byte of the input string */
    if (src) {
        size_t src_len = strlen(src);          /* compute once */
        for (size_t j = 0; j < src_len; ++j) {
            unsigned char c = (unsigned char)src[j];

            if (c == '\\' || c == '\"') {
                /* Escape backslash and double‑quote */
                if (pos + 2 >= dst_size) break;   /* not enough space */
                dst[pos++] = '\\';
                dst[pos++] = c;
            } else if (c < 0x20 || c == 0x7f) {
                /* Control character → octal escape \NNN */
                if (pos + 4 >= dst_size) break;
                dst[pos++] = '\\';
                dst[pos++] = '0' + ((c >> 6) & 7);
                dst[pos++] = '0' + ((c >> 3) & 7);
                dst[pos++] = '0' + (c & 7);
            } else {
                /* Regular byte – copy as‑is */
                if (pos + 1 >= dst_size) break;
                dst[pos++] = c;
            }
        }
    }

    /* Closing double quote */
    if (pos + 1 < dst_size) {
        dst[pos++] = '"';
    } else if (pos < dst_size) {
        /* No room for the closing quote – we still will NUL‑terminate */
    }

    /* Null‑terminate, ensuring we never write past dst_size - 1 */
    if (pos < dst_size) {
        dst[pos] = '\0';
    } else {
        dst[dst_size - 1] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/*  Get video duration in seconds using ffprobe                      */
/* ------------------------------------------------------------------ */
double get_video_duration(const char *video_path)
{
    if (!video_path)
        return -1.0;

    /* Build ffprobe command to get duration */
    char cmd[4096];
    int n = snprintf(
        cmd, sizeof(cmd),
        "ffprobe -v error -show_entries format=duration "
        "-of default=nokey=1:noprint_wrappers=1 \"%s\" 2>/dev/null",
        video_path
    );

    if (n < 0 || (size_t)n >= sizeof(cmd))
        return -1.0;

    FILE *fp = popen(cmd, "r");
    if (!fp)
        return -1.0;

    double duration = -1.0;
    char line[256];
    if (fgets(line, sizeof(line), fp)) {
        duration = strtod(line, NULL);
    }

    pclose(fp);
    return duration;
}


