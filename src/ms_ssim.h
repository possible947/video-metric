#ifndef MS_SSIM_H
#define MS_SSIM_H

#include "metrics_common.h"

/**
 * Compute MS-SSIM (Multi-Scale SSIM) between two videos.
 *
 * @param orig        Path to the reference (original) video.
 * @param test        Path to the distorted (test) video.
 * @param threads     Number of threads to use (0 for default).
 * @param stats       Output structure for min, max, mean MS-SSIM scores.
 * @param progress_cb Optional progress callback (NULL = print to stdout).
 * @param cb_userdata Opaque pointer forwarded to progress_cb.
 *
 * @return 0 on success, -1 on failure.
 */
int compute_ms_ssim(const char *orig, const char *test, int threads,
                    metric_stats *stats,
                    metric_progress_cb progress_cb, void *cb_userdata);

#endif /* MS_SSIM_H */
