/*====================================================================*/
/*  FILE: src/path_util.h                                           */
/*====================================================================*/
/**
 * @file   path_util.h
 * @brief  Public interface for shell‑safe path escaping.
 *
 * The sole public routine in this module is `escape_path()`.  It
 * converts an arbitrary UTF‑8 string into a form that can be safely
 * embedded inside a double‑quoted shell string.  The implementation
 * resides in `path_util.c`; this header provides the prototype
 * and documentation.
 *
 * @note  The function is *not* re‑entrant because it writes into a
 *        user‑supplied buffer.  The caller must ensure that the
 *        buffer is large enough for the worst‑case expansion.
 */

#ifndef PATH_UTIL_H
#define PATH_UTIL_H

#include <stddef.h>   /* size_t */

/**
 * @brief  Escape a file path for safe insertion into a double‑quoted
 *         shell string.
 *
 * @param src       Input string; may be NULL or empty.
 * @param dst       Destination buffer.
 * @param dst_size  Size of the destination buffer in bytes.
 *
 * @return Nothing.  The function guarantees that `dst` is always
 *         NUL‑terminated; if the buffer is too small the output is
 *         truncated but still NUL‑terminated.
 */
void escape_path(const char *src, char *dst, size_t dst_size);

/**
 * @brief  Get the duration of a video file in seconds using ffprobe.
 *
 * @param video_path  Path to the video file.
 *
 * @return Duration in seconds (double), or -1.0 on failure.
 */
double get_video_duration(const char *video_path);

#endif /* PATH_UTIL_H */

