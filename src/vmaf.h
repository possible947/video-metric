#ifndef VMAF_H
#define VMAF_H

#include <stddef.h>
#include "metrics_common.h"

/**
 * Compute VMAF between two videos using local ffmpeg and libvmaf.
 *
 * @param orig        Path to the reference (original) video.
 * @param test        Path to the distorted (test) video.
 * @param model_path  Path to the VMAF model JSON file.
 * @param threads     Number of threads for libvmaf (n_threads). 0 = detect CPUs.
 * @param json_path   Output buffer for the log file path ("vmaf.json").
 * @param json_bufsize Size of the json_path buffer.
 * @param stats       Output structure for min, max, mean VMAF scores.
 * @param progress_cb Optional progress callback (NULL = print to stdout).
 * @param cb_userdata Opaque pointer forwarded to progress_cb.
 *
 * @return 0 on success, -1 on failure.
 */
int compute_vmaf(const char *orig,
                 const char *test,
                 const char *model_path,
                 int         threads,
                 char       *json_path,
                 size_t      json_bufsize,
                 metric_stats *stats,
                 metric_progress_cb progress_cb,
                 void *cb_userdata);

/**
 * Return the backend used by the most recent successful compute_vmaf() call.
 * The returned string is static storage owned by the VMAF module.
 */
const char *vmaf_get_last_backend(void);

#endif /* VMAF_H */

