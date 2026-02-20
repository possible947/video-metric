#ifndef VMAF_H
#define VMAF_H

#include <stddef.h>

/**
 * Compute VMAF between two videos using local ffmpeg and libvmaf.
 *
 * @param orig        Path to the reference (original) video.
 * @param test        Path to the distorted (test) video.
 * @param model_path  Path to the VMAF model JSON file
 *                    (e.g. "model/vmaf_v0.6.1.json" or "model/vmaf_4k_v0.6.1.json").
 * @param threads     Number of threads for libvmaf (n_threads).
 *                    If threads <= 0, a default of 1 is used.
 * @param json_path   Output buffer for the log file path ("vmaf.json").
 * @param json_bufsize Size of the json_path buffer.
 *
 * @return VMAF score as double, or NAN on failure.
 */
double compute_vmaf(const char *orig,
                    const char *test,
                    const char *model_path,
                    int         threads,
                    char       *json_path,
                    size_t      json_bufsize);

#endif /* VMAF_H */

