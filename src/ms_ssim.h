#ifndef MS_SSIM_H
#define MS_SSIM_H

#include "metrics_common.h"

/**
 * Compute MS-SSIM (Multi-Scale SSIM) between two videos.
 *
 * @param orig     Path to the reference (original) video.
 * @param test     Path to the distorted (test) video.
 * @param threads  Number of threads to use (0 for default).
 * @param stats    Output structure for min, max, mean MS-SSIM scores.
 *
 * @return 0 on success, -1 on failure.
 */
int compute_ms_ssim(const char *orig, const char *test, int threads, metric_stats *stats);

#endif /* MS_SSIM_H */
