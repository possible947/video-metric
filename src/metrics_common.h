#ifndef METRICS_COMMON_H
#define METRICS_COMMON_H

/**
 * Structure to hold metric statistics
 */
typedef struct {
    double min;
    double max;
    double mean;
} metric_stats;

/**
 * Progress callback: called periodically during metric computation.
 * @param percent   0-100 completion estimate for the current metric.
 * @param userdata  Opaque pointer supplied by the caller.
 *
 * When NULL is passed as progress_cb the compute functions fall back
 * to printing a text progress bar to stdout (CLI behaviour).
 */
typedef void (*metric_progress_cb)(int percent, void *userdata);

#endif /* METRICS_COMMON_H */
