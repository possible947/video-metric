/*====================================================================*/
/*  FILE: src/cli_options.h                                          */
/*====================================================================*/
/**
 * @brief  Data structure holding parsed command‑line options.
 *
 * The structure is used by `cli_parser.c`, `main.c`, `ssim.c` and
 * `vmaf.c` to share the state of the program after parsing the
 * command‑line arguments.
 *
 * All members are intentionally simple to keep the interface
 * lightweight.  The calling code is responsible for allocating and
 * freeing the string members.
 *
 * @note  No functions are declared in this header; the implementation
 *        resides in the corresponding *.c files.
 */

#ifndef CLI_OPTIONS_H
#define CLI_OPTIONS_H

#include <stddef.h>   /* For NULL */

/** @struct cli_options
 *  @brief  Holds the command‑line configuration.
 *
 *  @var cli_options::orig
 *      Path to the original (reference) video.  Must be supplied.
 *
 *  @var cli_options::test
 *      Path to the test video.  Must be supplied.
 *
 *  @var cli_options::compute_ssim
 *      Non‑zero if the user requested SSIM computation
 *      (`-s` / `--ssim`).
 *
 *  @var cli_options::compute_vmaf
 *      Non‑zero if the user requested VMAF computation
 *      (`-v` / `--vmaf`).
 *
 *  @var cli_options::resolution
 *      Target resolution string.  Allowed values are `"hd"` or `"4k"`.
 *      Defaults to `"hd"`.
 *
 *  @var cli_options::num_threads
 *      Number of worker threads.  Zero means the program should
 *      choose an appropriate default.
 */
struct cli_options {
    char *orig;          /* Path to original video (required) */
    char *test;          /* Path to test video (required)   */
    int   compute_ssim;  /* 1 if SSIM flag was set         */
    int   compute_vmaf;  /* 1 if VMAF flag was set         */
    char *resolution;    /* "hd" or "4k", defaults to "hd"*/
    int   num_threads;   /* 0 = default thread count      */
};

#endif /* CLI_OPTIONS_H */

