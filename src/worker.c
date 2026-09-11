/*====================================================================*/
/*  FILE: src/worker.c                                               */
/*====================================================================*/
/*
 * Runs metric computations in a GTask thread pool thread.
 * Progress updates are marshalled to the GTK main thread through
 * g_idle_add() – each update is a heap-allocated worker_event that
 * the idle callback owns and must g_free().
 */

#include "worker.h"
#include "ssim.h"
#include "ms_ssim.h"
#include "vmaf.h"
#include "path_util.h"

#include <glib.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* ------------------------------------------------------------------ */
/*  Internal: per-run context shared between the thread and callbacks  */
/* ------------------------------------------------------------------ */
typedef struct {
    /* snapshot of options */
    char *orig_path;
    char *test_path;
    int   use_ssim;
    int   use_ms_ssim;
    int   use_vmaf;
    char *resolution;
    int   num_threads;

    /* communication */
    worker_idle_fn   idle_fn;
    void            *ctx;           /* ui_ctx* – opaque */
    volatile int    *cancel_flag;

    /* derived */
    int total_steps;
} run_ctx;

/* ------------------------------------------------------------------ */
/*  Helper: post a heap-allocated event to the main thread            */
/* ------------------------------------------------------------------ */
static void post_event(run_ctx *rc, worker_event *ev)
{
    ev->ctx = rc->ctx;
    g_idle_add(rc->idle_fn, ev);
}

/* ------------------------------------------------------------------ */
/*  Progress callback called from within compute_*                    */
/* ------------------------------------------------------------------ */
typedef struct {
    run_ctx         *rc;
    worker_metric_id metric;
    int              step_index;
} pcb_ctx;

static void on_metric_progress(int percent, void *userdata)
{
    pcb_ctx *pc = (pcb_ctx *)userdata;

    worker_event *ev = g_new0(worker_event, 1);
    ev->type        = WORKER_EVENT_METRIC_PROGRESS;
    ev->metric      = pc->metric;
    ev->percent     = percent;
    ev->step_index  = pc->step_index;
    ev->total_steps = pc->rc->total_steps;
    post_event(pc->rc, ev);
}

static const char *vmaf_backend_short_name(void)
{
    const char *backend = vmaf_get_last_backend();
    if (backend && strstr(backend, "CUDA"))
        return "cuda";
    if (backend && strstr(backend, "CPU"))
        return "cpu";
    return "";
}

/* ------------------------------------------------------------------ */
/*  Thread function                                                    */
/* ------------------------------------------------------------------ */
static void worker_thread(GTask        *task,
                          gpointer      source_object,
                          gpointer      task_data,
                          GCancellable *cancellable)
{
    (void)source_object;
    (void)cancellable;

    run_ctx *rc = (run_ctx *)task_data;
    int step = 0; /* 1-based counter of metrics actually started */

    /* ---- SSIM ---- */
    if (rc->use_ssim && !*rc->cancel_flag) {
        step++;
        pcb_ctx pc = { rc, WORKER_METRIC_SSIM, step };

        /* announce start (0%) */
        {
            worker_event *ev = g_new0(worker_event, 1);
            ev->type        = WORKER_EVENT_METRIC_PROGRESS;
            ev->metric      = WORKER_METRIC_SSIM;
            ev->percent     = 0;
            ev->step_index  = step;
            ev->total_steps = rc->total_steps;
            post_event(rc, ev);
        }

        metric_stats stats = {0};
        int ret = compute_ssim(rc->orig_path, rc->test_path,
                               rc->num_threads, &stats,
                               on_metric_progress, &pc);
        if (ret == 0) {
            worker_event *ev = g_new0(worker_event, 1);
            ev->type        = WORKER_EVENT_METRIC_DONE;
            ev->metric      = WORKER_METRIC_SSIM;
            ev->step_index  = step;
            ev->total_steps = rc->total_steps;
            ev->stats       = stats;
            post_event(rc, ev);
        } else {
            worker_event *ev = g_new0(worker_event, 1);
            ev->type    = WORKER_EVENT_METRIC_ERROR;
            ev->metric  = WORKER_METRIC_SSIM;
            g_snprintf(ev->message, sizeof(ev->message), "SSIM computation failed");
            post_event(rc, ev);
        }
    }

    /* ---- MS-SSIM ---- */
    if (rc->use_ms_ssim && !*rc->cancel_flag) {
        step++;
        pcb_ctx pc = { rc, WORKER_METRIC_MS_SSIM, step };

        {
            worker_event *ev = g_new0(worker_event, 1);
            ev->type        = WORKER_EVENT_METRIC_PROGRESS;
            ev->metric      = WORKER_METRIC_MS_SSIM;
            ev->percent     = 0;
            ev->step_index  = step;
            ev->total_steps = rc->total_steps;
            post_event(rc, ev);
        }

        metric_stats stats = {0};
        int ret = compute_ms_ssim(rc->orig_path, rc->test_path,
                                  rc->num_threads, &stats,
                                  on_metric_progress, &pc);
        if (ret == 0) {
            worker_event *ev = g_new0(worker_event, 1);
            ev->type        = WORKER_EVENT_METRIC_DONE;
            ev->metric      = WORKER_METRIC_MS_SSIM;
            ev->step_index  = step;
            ev->total_steps = rc->total_steps;
            ev->stats       = stats;
            post_event(rc, ev);
        } else {
            worker_event *ev = g_new0(worker_event, 1);
            ev->type    = WORKER_EVENT_METRIC_ERROR;
            ev->metric  = WORKER_METRIC_MS_SSIM;
            g_snprintf(ev->message, sizeof(ev->message), "MS-SSIM computation failed");
            post_event(rc, ev);
        }
    }

    /* ---- VMAF ---- */
    if (rc->use_vmaf && !*rc->cancel_flag) {
        step++;
        pcb_ctx pc = { rc, WORKER_METRIC_VMAF, step };

        {
            worker_event *ev = g_new0(worker_event, 1);
            ev->type        = WORKER_EVENT_METRIC_PROGRESS;
            ev->metric      = WORKER_METRIC_VMAF;
            ev->percent     = 0;
            ev->step_index  = step;
            ev->total_steps = rc->total_steps;
            post_event(rc, ev);
        }

        /* Build model path relative to executable directory */
        char exec_dir[PATH_MAX];
        char model_file[PATH_MAX];
        if (get_executable_dir(exec_dir, sizeof(exec_dir)) == 0) {
            if (rc->resolution && strcmp(rc->resolution, "4k") == 0)
                g_snprintf(model_file, sizeof(model_file),
                           "%s/model/vmaf_4k_v0.6.1.json", exec_dir);
            else
                g_snprintf(model_file, sizeof(model_file),
                           "%s/model/vmaf_v0.6.1.json", exec_dir);
        } else {
            /* Fallback: relative path */
            if (rc->resolution && strcmp(rc->resolution, "4k") == 0)
                g_snprintf(model_file, sizeof(model_file),
                           "model/vmaf_4k_v0.6.1.json");
            else
                g_snprintf(model_file, sizeof(model_file),
                           "model/vmaf_v0.6.1.json");
        }

        char json_path[PATH_MAX];
        metric_stats stats = {0};
        int ret = compute_vmaf(rc->orig_path, rc->test_path,
                               model_file, rc->num_threads,
                               json_path, sizeof(json_path),
                               &stats, on_metric_progress, &pc);
        if (ret == 0) {
            worker_event *ev = g_new0(worker_event, 1);
            ev->type        = WORKER_EVENT_METRIC_DONE;
            ev->metric      = WORKER_METRIC_VMAF;
            ev->step_index  = step;
            ev->total_steps = rc->total_steps;
            ev->stats       = stats;
            g_strlcpy(ev->backend, vmaf_backend_short_name(), sizeof(ev->backend));
            post_event(rc, ev);
        } else {
            worker_event *ev = g_new0(worker_event, 1);
            ev->type    = WORKER_EVENT_METRIC_ERROR;
            ev->metric  = WORKER_METRIC_VMAF;
            g_snprintf(ev->message, sizeof(ev->message), "VMAF computation failed");
            post_event(rc, ev);
        }
    }

    /* ---- All done ---- */
    {
        worker_event *ev = g_new0(worker_event, 1);
        ev->type    = WORKER_EVENT_ALL_DONE;
        ev->success = (*rc->cancel_flag) ? 0 : 1;
        if (*rc->cancel_flag)
            g_snprintf(ev->message, sizeof(ev->message), "Cancelled");
        post_event(rc, ev);
    }

    g_task_return_boolean(task, TRUE);
}

/* ------------------------------------------------------------------ */
/*  run_ctx cleanup                                                    */
/* ------------------------------------------------------------------ */
static void run_ctx_free(gpointer data)
{
    run_ctx *rc = (run_ctx *)data;
    if (!rc) return;
    g_free(rc->orig_path);
    g_free(rc->test_path);
    g_free(rc->resolution);
    g_free(rc);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */
void worker_start(const app_state *state,
                  worker_idle_fn   idle_fn,
                  void            *ctx,
                  volatile int    *cancel_flag)
{
    run_ctx *rc = g_new0(run_ctx, 1);
    rc->orig_path   = g_strdup(state->orig_path  ? state->orig_path  : "");
    rc->test_path   = g_strdup(state->test_path  ? state->test_path  : "");
    rc->use_ssim    = state->use_ssim;
    rc->use_ms_ssim = state->use_ms_ssim;
    rc->use_vmaf    = state->use_vmaf;
    rc->resolution  = g_strdup(state->resolution ? state->resolution : "hd");
    rc->num_threads = state->num_threads;
    rc->idle_fn     = idle_fn;
    rc->ctx         = ctx;
    rc->cancel_flag = cancel_flag;
    rc->total_steps = app_state_count_metrics(state);

    GTask *task = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(task, rc, run_ctx_free);
    g_task_run_in_thread(task, worker_thread);
    g_object_unref(task);
}
