/*====================================================================*/
/*  FILE: src/env_check.h                                           */
/*====================================================================*/
/**
 * @file   env_check.h
 * @brief  Declaration of the runtime environment check routine.
 *
 * The function `check_environment()` performs a series of sanity checks
 * before the metric calculations are attempted:
 *
 *   • Verifies that the local `ffmpeg` binary is present and
 *     executable.
 *   • Confirms that the `ssim` and `libvmaf` filters are
 *     available via `ffmpeg -filters`.
 *   • Checks for the existence of the required VMAF model files
 *     (`model/vmaf_v0.6.1.json` and `model/vmaf_4k_v0.6.1.json`).
 *   • Executes a minimal VMAF run to ensure the filter works
 *     correctly.
 *
 * The results are returned in a status string of the form
 *
 *     ffmpeg: <status>, ssim: <status>, vmaf: <status>
 *
 * where each `<status>` is either `ok` or `fail`.  The function returns
 * `0` if all checks succeed, otherwise it returns `-1` and prints
 * diagnostic information to `stderr`.
 *
 * @author  OpenAI ChatGPT
 * @date    2026‑02‑13
 */

#ifndef ENV_CHECK_H
#define ENV_CHECK_H

#include <stddef.h>   /* for size_t */

/**
 * @brief  Verify that the required runtime components are available.
 *
 * @param[out] status_buf  Buffer to receive the status string.
 * @param[in]  bufsize     Size of the status buffer in bytes.
 *
 * @return 0 on success, -1 on failure.
 *
 * @note   The caller must ensure that `status_buf` is large enough to
 *         hold the entire status line plus a terminating NUL.
 */
int check_environment(char *status_buf, size_t bufsize);

#endif /* ENV_CHECK_H */

