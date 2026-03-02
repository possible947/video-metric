#ifndef SSIM_H
#define SSIM_H

#include "metrics_common.h"

int compute_ssim(const char *orig, const char *test, int threads, metric_stats *stats);

#endif

