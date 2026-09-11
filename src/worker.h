/*====================================================================*/
/*  FILE: src/worker.h                                               */
/*====================================================================*/
#ifndef WORKER_H
#define WORKER_H

#include <glib.h>
#include "app_state.h"
#include "metrics_common.h"

/* ------------------------------------------------------------------ */
/*  Event types sent from the worker thread to the UI via g_idle_add  */
/* ------------------------------------------------------------------ */
typedef enum {
    WORKER_EVENT_METRIC_PROGRESS,  /* periodic progress within a metric  */
    WORKER_EVENT_METRIC_DONE,      /* one metric finished successfully    */
    WORKER_EVENT_METRIC_ERROR,     /* one metric failed                   */
    WORKER_EVENT_ALL_DONE,         /* all metrics finished                */
} worker_event_type;

typedef enum {
    WORKER_METRIC_SSIM = 0,
    WORKER_METRIC_MS_SSIM,
    WORKER_METRIC_VMAF,
} worker_metric_id;

/* Heap-allocated packet posted via g_idle_add */
typedef struct {
    worker_event_type  type;
    worker_metric_id   metric;

    /* WORKER_EVENT_METRIC_PROGRESS */
    int                percent;      /* 0-100 for current metric              */
    int                step_index;   /* 1-based index of this metric in run   */
    int                total_steps;  /* total metrics selected for this run   */

    /* WORKER_EVENT_METRIC_DONE */
    metric_stats       stats;
    char               backend[16];  /* VMAF only: "cpu", "cuda", or empty */

    /* WORKER_EVENT_METRIC_ERROR / ALL_DONE */
    char               message[256];
    int                success;      /* 1 = all OK (ALL_DONE only)            */

    /* back-pointer to owning context – set by caller */
    void              *ctx;          /* ui_ctx* – opaque here                 */
} worker_event;

/* ------------------------------------------------------------------ */
/*  Worker public API                                                  */
/* ------------------------------------------------------------------ */

/*
 * Signature of the idle callback that processes worker events.
 * Register it with: g_idle_add(worker_idle_dispatch, event_ptr)
 * The callback is responsible for g_free(event_ptr).
 */
typedef gboolean (*worker_idle_fn)(gpointer event);

/*
 * Start a background GTask that runs the selected metrics.
 *
 * @param state         Application state snapshot (paths/options read here).
 * @param idle_fn       Callback to post events to the main thread.
 * @param ctx           Opaque pointer (ui_ctx*) embedded in every event.
 * @param cancel_flag   Pointer to an int; worker stops between metrics
 *                      if *cancel_flag becomes non-zero.
 */
void worker_start(const app_state *state,
                  worker_idle_fn   idle_fn,
                  void            *ctx,
                  volatile int    *cancel_flag);

#endif /* WORKER_H */
