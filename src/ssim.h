#ifndef SSIM_H
#define SSIM_H

#include "metrics_common.h"

/**
 * Compute SSIM between two videos using the local ffmpeg ssim filter.
 *
 * @param progress_cb  Optional progress callback (NULL = print to stdout).
 * @param cb_userdata  Opaque pointer forwarded to progress_cb.
 */
int compute_ssim(const char *orig, const char *test, int threads,
                 metric_stats *stats,
                 metric_progress_cb progress_cb, void *cb_userdata);

#endif

