#ifndef APP_STATE_H
#define APP_STATE_H

#include <glib.h>

typedef struct metric_result {
    gboolean available;
    double min;
    double max;
    double mean;
} metric_result;

typedef struct app_state {
    char *orig_path;
    char *test_path;
    gboolean use_ssim;
    gboolean use_ms_ssim;
    gboolean use_vmaf;
    char *resolution;
    int num_threads;

    gboolean running;

    metric_result ssim;
    metric_result ms_ssim;
    metric_result vmaf;
} app_state;

void app_state_init(app_state *state);
void app_state_reset_results(app_state *state);
void app_state_clear_paths(app_state *state);
void app_state_free(app_state *state);

#endif

