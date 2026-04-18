/*====================================================================*/
/*  FILE: src/app_state.h                                            */
/*====================================================================*/
#ifndef APP_STATE_H
#define APP_STATE_H

#include "metrics_common.h"

/* ------------------------------------------------------------------ */
/*  Per-metric result                                                  */
/* ------------------------------------------------------------------ */
typedef struct {
    int          available; /* 1 when result has been computed */
    metric_stats stats;
} metric_result;

/* ------------------------------------------------------------------ */
/*  Application state (UI-owned, accessed only from the main thread)  */
/* ------------------------------------------------------------------ */
typedef struct {
    /* --- Input paths --- */
    char *orig_path;
    char *test_path;

    /* --- Metric selection --- */
    int use_ssim;
    int use_ms_ssim;
    int use_vmaf;

    /* --- VMAF options --- */
    char *resolution;   /* "hd" or "4k" */

    /* --- Threading --- */
    int num_threads;    /* 0 = let ffmpeg decide */

    /* --- Runtime --- */
    int running;        /* 1 while worker is active */

    /* --- Results --- */
    metric_result ssim;
    metric_result ms_ssim;
    metric_result vmaf;
} app_state;

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */
void app_state_init(app_state *s);
void app_state_reset_results(app_state *s);
void app_state_free(app_state *s);

/* Returns the number of metrics currently selected (0-3). */
int  app_state_count_metrics(const app_state *s);

#endif /* APP_STATE_H */
