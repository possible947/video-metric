/*====================================================================*/
/*  FILE: src/ui_main.c                                              */
/*====================================================================*/
/*
 * GTK4 window for video_metric_gui.
 *
 * Layout
 * ------
 *  GtkApplicationWindow
 *    GtkHeaderBar  (title)
 *    GtkBox (vertical, main_box)
 *      GtkFrame "Input"
 *        GtkGrid  orig-row / test-row
 *      GtkFrame "Options"
 *        GtkBox (horizontal)
 *          GtkBox (vertical) – metric checkboxes
 *          GtkSeparator
 *          GtkBox (vertical) – resolution (shown only for VMAF)
 *          GtkSeparator
 *          GtkBox (vertical) – threads spinner
 *      GtkBox (horizontal) – Run / Cancel buttons
 *      GtkFrame "Progress"
 *        GtkBox (vertical)
 *          GtkLabel  status_label
 *          GtkLabel  "Overall:"
 *          GtkProgressBar  global_bar   (shows text "Step N/M")
 *          GtkLabel  "Current metric:"
 *          GtkProgressBar  metric_bar   (shows text "XX%")
 *      GtkRevealer – error panel
 *        GtkLabel  error_label
 *      GtkBox (horizontal) – result frames
 *        GtkFrame "SSIM"   (Min / Max / Mean value labels)
 *        GtkFrame "MS-SSIM"
 *        GtkFrame "VMAF"
 */

#include "ui_main.h"
#include "app_state.h"
#include "worker.h"
#include "env_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  UI context (one per window)                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    app_state     state;
    volatile int  cancel_flag;

    /* --- Window widgets --- */
    GtkWidget *window;

    /* Input */
    GtkWidget *orig_button;
    GtkWidget *orig_label;
    GtkWidget *test_button;
    GtkWidget *test_label;

    /* Options */
    GtkWidget *ssim_check;
    GtkWidget *ms_ssim_check;
    GtkWidget *vmaf_check;
    GtkWidget *res_hd_btn;
    GtkWidget *res_4k_btn;
    GtkWidget *threads_spin;

    /* Actions */
    GtkWidget *run_button;
    GtkWidget *cancel_button;

    /* Progress */
    GtkWidget *status_label;
    GtkWidget *global_bar;
    GtkWidget *metric_bar;

    /* Error panel */
    GtkWidget *error_revealer;
    GtkWidget *error_label;

    /* Result labels – each metric has min/max/mean */
    GtkWidget *ssim_min,    *ssim_max,    *ssim_mean;
    GtkWidget *ms_ssim_min, *ms_ssim_max, *ms_ssim_mean;
    GtkWidget *vmaf_frame;
    GtkWidget *vmaf_min,    *vmaf_max,    *vmaf_mean;
} ui_ctx;

/* ================================================================== */
/*  Helpers                                                            */
/* ================================================================== */

static const char *metric_name(worker_metric_id id)
{
    switch (id) {
    case WORKER_METRIC_SSIM:    return "SSIM";
    case WORKER_METRIC_MS_SSIM: return "MS-SSIM";
    case WORKER_METRIC_VMAF:    return "VMAF";
    default:                    return "?";
    }
}

static void show_error(ui_ctx *ctx, const char *msg)
{
    gtk_label_set_text(GTK_LABEL(ctx->error_label), msg ? msg : "");
    gtk_revealer_set_reveal_child(GTK_REVEALER(ctx->error_revealer),
                                  msg && *msg ? TRUE : FALSE);
}

static void hide_error(ui_ctx *ctx)
{
    show_error(ctx, NULL);
}

static void set_result_labels(GtkWidget *wmin, GtkWidget *wmax, GtkWidget *wmean,
                               const metric_stats *s)
{
    char buf[32];
    g_snprintf(buf, sizeof(buf), "%.6f", s->min);
    gtk_label_set_text(GTK_LABEL(wmin), buf);
    g_snprintf(buf, sizeof(buf), "%.6f", s->max);
    gtk_label_set_text(GTK_LABEL(wmax), buf);
    g_snprintf(buf, sizeof(buf), "%.6f", s->mean);
    gtk_label_set_text(GTK_LABEL(wmean), buf);
}

static void reset_result_labels(ui_ctx *ctx)
{
    const char *dash = "–";
    gtk_label_set_text(GTK_LABEL(ctx->ssim_min),    dash);
    gtk_label_set_text(GTK_LABEL(ctx->ssim_max),    dash);
    gtk_label_set_text(GTK_LABEL(ctx->ssim_mean),   dash);
    gtk_label_set_text(GTK_LABEL(ctx->ms_ssim_min), dash);
    gtk_label_set_text(GTK_LABEL(ctx->ms_ssim_max), dash);
    gtk_label_set_text(GTK_LABEL(ctx->ms_ssim_mean),dash);
    gtk_label_set_text(GTK_LABEL(ctx->vmaf_min),    dash);
    gtk_label_set_text(GTK_LABEL(ctx->vmaf_max),    dash);
    gtk_label_set_text(GTK_LABEL(ctx->vmaf_mean),   dash);
    gtk_frame_set_label(GTK_FRAME(ctx->vmaf_frame), "VMAF");
}

static void refresh_sensitivity(ui_ctx *ctx)
{
    gboolean running = (gboolean)ctx->state.running;
    gtk_widget_set_sensitive(ctx->orig_button,       !running);
    gtk_widget_set_sensitive(ctx->test_button,       !running);
    gtk_widget_set_sensitive(ctx->ssim_check,        !running);
    gtk_widget_set_sensitive(ctx->ms_ssim_check,     !running);
    gtk_widget_set_sensitive(ctx->vmaf_check,        !running);
    gtk_widget_set_sensitive(ctx->res_hd_btn, !running && ctx->state.use_vmaf);
    gtk_widget_set_sensitive(ctx->res_4k_btn, !running && ctx->state.use_vmaf);
    gtk_widget_set_sensitive(ctx->threads_spin,      !running);
    gtk_widget_set_sensitive(ctx->run_button,        !running);
    gtk_widget_set_sensitive(ctx->cancel_button,      running);
}

/* ================================================================== */
/*  Worker event idle handler (called on main thread)                  */
/* ================================================================== */

static gboolean worker_idle_dispatch(gpointer data)
{
    worker_event *ev  = (worker_event *)data;
    ui_ctx       *ctx = (ui_ctx *)ev->ctx;

    if (!ctx) {
        g_free(ev);
        return G_SOURCE_REMOVE;
    }

    switch (ev->type) {

    case WORKER_EVENT_METRIC_PROGRESS: {
        /* --- current metric bar --- */
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->metric_bar),
                                      ev->percent / 100.0);
        char pct_text[16];
        g_snprintf(pct_text, sizeof(pct_text), "%d%%", ev->percent);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->metric_bar), pct_text);

        /* --- global bar: (completed_steps + current_fraction) / total --- */
        double global = 0.0;
        if (ev->total_steps > 0) {
            double completed = (double)(ev->step_index - 1);
            global = (completed + ev->percent / 100.0) / (double)ev->total_steps;
        }
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->global_bar), global);

        char step_text[64];
        g_snprintf(step_text, sizeof(step_text),
                   "Step %d / %d  –  %s",
                   ev->step_index, ev->total_steps,
                   metric_name(ev->metric));
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->global_bar), step_text);

        char status[128];
        g_snprintf(status, sizeof(status),
                   "Computing %s…", metric_name(ev->metric));
        gtk_label_set_text(GTK_LABEL(ctx->status_label), status);
        break;
    }

    case WORKER_EVENT_METRIC_DONE: {
        /* Fill result frame */
        switch (ev->metric) {
        case WORKER_METRIC_SSIM:
            ctx->state.ssim.available = 1;
            ctx->state.ssim.stats     = ev->stats;
            set_result_labels(ctx->ssim_min, ctx->ssim_max, ctx->ssim_mean,
                              &ev->stats);
            break;
        case WORKER_METRIC_MS_SSIM:
            ctx->state.ms_ssim.available = 1;
            ctx->state.ms_ssim.stats     = ev->stats;
            set_result_labels(ctx->ms_ssim_min, ctx->ms_ssim_max, ctx->ms_ssim_mean,
                              &ev->stats);
            break;
        case WORKER_METRIC_VMAF:
            ctx->state.vmaf.available = 1;
            ctx->state.vmaf.stats     = ev->stats;
            if (ev->backend[0]) {
                char title[32];
                g_snprintf(title, sizeof(title), "VMAF (%s)", ev->backend);
                gtk_frame_set_label(GTK_FRAME(ctx->vmaf_frame), title);
            }
            set_result_labels(ctx->vmaf_min, ctx->vmaf_max, ctx->vmaf_mean,
                              &ev->stats);
            break;
        }
        /* Drive current metric bar to 100% */
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->metric_bar), 1.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->metric_bar), "100%");

        /* Advance global bar to end of this step */
        if (ev->total_steps > 0) {
            double global = (double)ev->step_index / (double)ev->total_steps;
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->global_bar), global);
        }
        break;
    }

    case WORKER_EVENT_METRIC_ERROR: {
        show_error(ctx, ev->message);
        break;
    }

    case WORKER_EVENT_ALL_DONE: {
        ctx->state.running = 0;
        ctx->cancel_flag   = 0;
        refresh_sensitivity(ctx);

        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->global_bar),
                                      ev->success ? 1.0 : 0.0);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->metric_bar),
                                      ev->success ? 1.0 : 0.0);

        if (ev->success) {
            gtk_label_set_text(GTK_LABEL(ctx->status_label), "Done");
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->global_bar),
                                      "Complete");
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->metric_bar), "");
        } else {
            gtk_label_set_text(GTK_LABEL(ctx->status_label),
                               ev->message[0] ? ev->message : "Stopped");
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->global_bar),
                                      ev->message[0] ? ev->message : "Stopped");
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->metric_bar), "");
        }
        break;
    }
    }

    g_free(ev);
    return G_SOURCE_REMOVE;
}

/* ================================================================== */
/*  Button callbacks                                                   */
/* ================================================================== */

/* ----- File chooser (original) ----- */
static void orig_chosen_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    ui_ctx        *ctx    = (ui_ctx *)user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError        *err    = NULL;

    GFile *file = gtk_file_dialog_open_finish(dialog, res, &err);
    if (file) {
        g_free(ctx->state.orig_path);
        ctx->state.orig_path = g_file_get_path(file);
        char *base = g_path_get_basename(ctx->state.orig_path);
        gtk_label_set_text(GTK_LABEL(ctx->orig_label), base);
        g_free(base);
        g_object_unref(file);
    }
    if (err) g_error_free(err);
}

static void on_choose_orig(GtkButton *button, gpointer user_data)
{
    (void)button;
    ui_ctx        *ctx    = (ui_ctx *)user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Original Video");
    gtk_file_dialog_open(dialog, GTK_WINDOW(ctx->window),
                         NULL, orig_chosen_cb, ctx);
    g_object_unref(dialog);
}

/* ----- File chooser (test) ----- */
static void test_chosen_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    ui_ctx        *ctx    = (ui_ctx *)user_data;
    GtkFileDialog *dialog = GTK_FILE_DIALOG(source);
    GError        *err    = NULL;

    GFile *file = gtk_file_dialog_open_finish(dialog, res, &err);
    if (file) {
        g_free(ctx->state.test_path);
        ctx->state.test_path = g_file_get_path(file);
        char *base = g_path_get_basename(ctx->state.test_path);
        gtk_label_set_text(GTK_LABEL(ctx->test_label), base);
        g_free(base);
        g_object_unref(file);
    }
    if (err) g_error_free(err);
}

static void on_choose_test(GtkButton *button, gpointer user_data)
{
    (void)button;
    ui_ctx        *ctx    = (ui_ctx *)user_data;
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select Test Video");
    gtk_file_dialog_open(dialog, GTK_WINDOW(ctx->window),
                         NULL, test_chosen_cb, ctx);
    g_object_unref(dialog);
}

/* ----- Metric checkboxes ----- */
static void on_metric_toggled(GtkCheckButton *btn, gpointer user_data)
{
    (void)btn;
    ui_ctx *ctx = (ui_ctx *)user_data;
    ctx->state.use_ssim    = gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->ssim_check));
    ctx->state.use_ms_ssim = gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->ms_ssim_check));
    ctx->state.use_vmaf    = gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->vmaf_check));
    gtk_widget_set_sensitive(ctx->res_hd_btn, !ctx->state.running && ctx->state.use_vmaf);
    gtk_widget_set_sensitive(ctx->res_4k_btn, !ctx->state.running && ctx->state.use_vmaf);
}

/* ----- Resolution radio buttons ----- */
static void on_res_toggled(GtkCheckButton *btn, gpointer user_data)
{
    (void)btn;
    ui_ctx *ctx = (ui_ctx *)user_data;
    g_free(ctx->state.resolution);
    ctx->state.resolution = g_strdup(
        gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->res_4k_btn)) ? "4k" : "hd");
}

/* ----- Threads spinner ----- */
static void on_threads_changed(GtkSpinButton *spin, gpointer user_data)
{
    ui_ctx *ctx = (ui_ctx *)user_data;
    ctx->state.num_threads = (int)gtk_spin_button_get_value(spin);
}

/* ----- Run ----- */
static void on_run_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ui_ctx *ctx = (ui_ctx *)user_data;

    /* Validate paths */
    if (!ctx->state.orig_path || ctx->state.orig_path[0] == '\0') {
        show_error(ctx, "Please select the original video.");
        return;
    }
    if (!ctx->state.test_path || ctx->state.test_path[0] == '\0') {
        show_error(ctx, "Please select the test video.");
        return;
    }
    if (app_state_count_metrics(&ctx->state) == 0) {
        show_error(ctx, "Please select at least one metric.");
        return;
    }

    hide_error(ctx);
    reset_result_labels(ctx);
    app_state_reset_results(&ctx->state);

    /* Reset progress bars */
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->global_bar), 0.0);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->metric_bar), 0.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->global_bar), "");
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->metric_bar), "");
    gtk_label_set_text(GTK_LABEL(ctx->status_label), "Starting…");

    ctx->state.running = 1;
    ctx->cancel_flag   = 0;
    refresh_sensitivity(ctx);

    worker_start(&ctx->state,
                 (worker_idle_fn)worker_idle_dispatch,
                 ctx,
                 &ctx->cancel_flag);
}

/* ----- Cancel ----- */
static void on_cancel_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    ui_ctx *ctx    = (ui_ctx *)user_data;
    ctx->cancel_flag = 1;
    gtk_label_set_text(GTK_LABEL(ctx->status_label), "Cancelling…");
    gtk_widget_set_sensitive(ctx->cancel_button, FALSE);
}

/* ================================================================== */
/*  Widget builders                                                    */
/* ================================================================== */

/* Helper: make a result frame (SSIM / MS-SSIM / VMAF) */
static GtkWidget *make_result_frame(const char  *title,
                                    GtkWidget  **out_min,
                                    GtkWidget  **out_max,
                                    GtkWidget  **out_mean)
{
    GtkWidget *frame = gtk_frame_new(title);
    GtkWidget *grid  = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_widget_set_margin_start(grid, 8);
    gtk_widget_set_margin_end(grid, 8);
    gtk_widget_set_margin_top(grid, 6);
    gtk_widget_set_margin_bottom(grid, 6);

    const char *labels[] = { "Min:", "Max:", "Mean:" };
    GtkWidget  *vals[3];

    for (int i = 0; i < 3; i++) {
        GtkWidget *lbl = gtk_label_new(labels[i]);
        gtk_label_set_xalign(GTK_LABEL(lbl), 1.0f);
        gtk_grid_attach(GTK_GRID(grid), lbl, 0, i, 1, 1);

        vals[i] = gtk_label_new("–");
        gtk_label_set_xalign(GTK_LABEL(vals[i]), 0.0f);
        gtk_label_set_selectable(GTK_LABEL(vals[i]), TRUE);
        gtk_grid_attach(GTK_GRID(grid), vals[i], 1, i, 1, 1);
    }

    gtk_frame_set_child(GTK_FRAME(frame), grid);

    *out_min  = vals[0];
    *out_max  = vals[1];
    *out_mean = vals[2];
    return frame;
}

/* Helper: one row of the input grid */
static void make_input_row(GtkGrid     *grid,
                            int          row,
                            const char  *label_text,
                            GtkWidget  **out_button,
                            GtkWidget  **out_label,
                            ui_ctx      *ctx,
                            GCallback    cb)
{
    GtkWidget *lbl = gtk_label_new(label_text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 1.0f);
    gtk_grid_attach(grid, lbl, 0, row, 1, 1);

    *out_button = gtk_button_new_with_label("Choose…");
    gtk_grid_attach(grid, *out_button, 1, row, 1, 1);

    *out_label = gtk_label_new("(none)");
    gtk_label_set_xalign(GTK_LABEL(*out_label), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(*out_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(*out_label), 40);
    gtk_grid_attach(grid, *out_label, 2, row, 1, 1);

    g_signal_connect(*out_button, "clicked", cb, ctx);
}

/* ================================================================== */
/*  Window builder                                                     */
/* ================================================================== */

static void build_window(ui_ctx *ctx, GtkApplication *app)
{
    /* Top-level window */
    ctx->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(ctx->window), "Video Metric");
    gtk_window_set_default_size(GTK_WINDOW(ctx->window), 680, -1);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(main_box, 12);
    gtk_widget_set_margin_end(main_box, 12);
    gtk_widget_set_margin_top(main_box, 12);
    gtk_widget_set_margin_bottom(main_box, 12);
    gtk_window_set_child(GTK_WINDOW(ctx->window), main_box);

    /* ---- Input frame ---- */
    {
        GtkWidget *frame = gtk_frame_new("Input");
        GtkWidget *grid  = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
        gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
        gtk_widget_set_margin_start(grid, 8);
        gtk_widget_set_margin_end(grid, 8);
        gtk_widget_set_margin_top(grid, 6);
        gtk_widget_set_margin_bottom(grid, 6);

        make_input_row(GTK_GRID(grid), 0, "Original:",
                       &ctx->orig_button, &ctx->orig_label,
                       ctx, G_CALLBACK(on_choose_orig));
        make_input_row(GTK_GRID(grid), 1, "Test:",
                       &ctx->test_button, &ctx->test_label,
                       ctx, G_CALLBACK(on_choose_test));

        gtk_frame_set_child(GTK_FRAME(frame), grid);
        gtk_box_append(GTK_BOX(main_box), frame);
    }

    /* ---- Options frame ---- */
    {
        GtkWidget *frame   = gtk_frame_new("Options");
        GtkWidget *hbox    = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
        gtk_widget_set_margin_start(hbox, 8);
        gtk_widget_set_margin_end(hbox, 8);
        gtk_widget_set_margin_top(hbox, 6);
        gtk_widget_set_margin_bottom(hbox, 6);

        /* Metric checkboxes */
        GtkWidget *metrics_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *metrics_lbl = gtk_label_new("Metrics");
        gtk_label_set_xalign(GTK_LABEL(metrics_lbl), 0.0f);
        gtk_box_append(GTK_BOX(metrics_box), metrics_lbl);

        ctx->ssim_check    = gtk_check_button_new_with_label("SSIM");
        ctx->ms_ssim_check = gtk_check_button_new_with_label("MS-SSIM");
        ctx->vmaf_check    = gtk_check_button_new_with_label("VMAF");
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ctx->ssim_check),    TRUE);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ctx->ms_ssim_check), TRUE);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ctx->vmaf_check),    TRUE);
        ctx->state.use_ssim    = 1;
        ctx->state.use_ms_ssim = 1;
        ctx->state.use_vmaf    = 1;

        g_signal_connect(ctx->ssim_check,    "toggled", G_CALLBACK(on_metric_toggled), ctx);
        g_signal_connect(ctx->ms_ssim_check, "toggled", G_CALLBACK(on_metric_toggled), ctx);
        g_signal_connect(ctx->vmaf_check,    "toggled", G_CALLBACK(on_metric_toggled), ctx);

        gtk_box_append(GTK_BOX(metrics_box), ctx->ssim_check);
        gtk_box_append(GTK_BOX(metrics_box), ctx->ms_ssim_check);
        gtk_box_append(GTK_BOX(metrics_box), ctx->vmaf_check);
        gtk_box_append(GTK_BOX(hbox), metrics_box);

        gtk_box_append(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

        /* Resolution */
        GtkWidget *res_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *res_lbl = gtk_label_new("VMAF Resolution");
        gtk_label_set_xalign(GTK_LABEL(res_lbl), 0.0f);
        gtk_box_append(GTK_BOX(res_box), res_lbl);

        ctx->res_hd_btn = gtk_check_button_new_with_label("HD (1080p)");
        ctx->res_4k_btn = gtk_check_button_new_with_label("4K (2160p)");
        gtk_check_button_set_group(GTK_CHECK_BUTTON(ctx->res_4k_btn),
                                   GTK_CHECK_BUTTON(ctx->res_hd_btn));
        gtk_check_button_set_active(GTK_CHECK_BUTTON(ctx->res_hd_btn), TRUE);
        g_signal_connect(ctx->res_hd_btn, "toggled", G_CALLBACK(on_res_toggled), ctx);
        g_signal_connect(ctx->res_4k_btn, "toggled", G_CALLBACK(on_res_toggled), ctx);
        gtk_box_append(GTK_BOX(res_box), ctx->res_hd_btn);
        gtk_box_append(GTK_BOX(res_box), ctx->res_4k_btn);
        gtk_box_append(GTK_BOX(hbox), res_box);

        gtk_box_append(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL));

        /* Threads */
        GtkWidget *thr_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *thr_lbl = gtk_label_new("Threads (0 = auto)");
        gtk_label_set_xalign(GTK_LABEL(thr_lbl), 0.0f);
        gtk_box_append(GTK_BOX(thr_box), thr_lbl);

        GtkAdjustment *adj = gtk_adjustment_new(0, 0, 64, 1, 4, 0);
        ctx->threads_spin = gtk_spin_button_new(adj, 1.0, 0);
        g_signal_connect(ctx->threads_spin, "value-changed",
                         G_CALLBACK(on_threads_changed), ctx);
        gtk_box_append(GTK_BOX(thr_box), ctx->threads_spin);
        gtk_box_append(GTK_BOX(hbox), thr_box);

        gtk_frame_set_child(GTK_FRAME(frame), hbox);
        gtk_box_append(GTK_BOX(main_box), frame);
    }

    /* ---- Action buttons ---- */
    {
        GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_halign(btn_box, GTK_ALIGN_END);

        ctx->run_button = gtk_button_new_with_label("Run");
        gtk_widget_add_css_class(ctx->run_button, "suggested-action");
        g_signal_connect(ctx->run_button, "clicked",
                         G_CALLBACK(on_run_clicked), ctx);

        ctx->cancel_button = gtk_button_new_with_label("Cancel");
        gtk_widget_set_sensitive(ctx->cancel_button, FALSE);
        g_signal_connect(ctx->cancel_button, "clicked",
                         G_CALLBACK(on_cancel_clicked), ctx);

        gtk_box_append(GTK_BOX(btn_box), ctx->run_button);
        gtk_box_append(GTK_BOX(btn_box), ctx->cancel_button);
        gtk_box_append(GTK_BOX(main_box), btn_box);
    }

    /* ---- Progress frame ---- */
    {
        GtkWidget *frame   = gtk_frame_new("Progress");
        GtkWidget *pbox    = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_margin_start(pbox, 8);
        gtk_widget_set_margin_end(pbox, 8);
        gtk_widget_set_margin_top(pbox, 6);
        gtk_widget_set_margin_bottom(pbox, 6);

        ctx->status_label = gtk_label_new("Ready");
        gtk_label_set_xalign(GTK_LABEL(ctx->status_label), 0.0f);
        gtk_box_append(GTK_BOX(pbox), ctx->status_label);

        GtkWidget *overall_lbl = gtk_label_new("Overall:");
        gtk_label_set_xalign(GTK_LABEL(overall_lbl), 0.0f);
        gtk_box_append(GTK_BOX(pbox), overall_lbl);

        ctx->global_bar = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ctx->global_bar), TRUE);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->global_bar), "");
        gtk_widget_set_hexpand(ctx->global_bar, TRUE);
        gtk_box_append(GTK_BOX(pbox), ctx->global_bar);

        GtkWidget *metric_lbl = gtk_label_new("Current metric:");
        gtk_label_set_xalign(GTK_LABEL(metric_lbl), 0.0f);
        gtk_box_append(GTK_BOX(pbox), metric_lbl);

        ctx->metric_bar = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ctx->metric_bar), TRUE);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ctx->metric_bar), "");
        gtk_widget_set_hexpand(ctx->metric_bar, TRUE);
        gtk_box_append(GTK_BOX(pbox), ctx->metric_bar);

        gtk_frame_set_child(GTK_FRAME(frame), pbox);
        gtk_box_append(GTK_BOX(main_box), frame);
    }

    /* ---- Error revealer ---- */
    {
        ctx->error_revealer = gtk_revealer_new();
        gtk_revealer_set_transition_type(
            GTK_REVEALER(ctx->error_revealer),
            GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
        gtk_revealer_set_reveal_child(GTK_REVEALER(ctx->error_revealer), FALSE);

        GtkWidget *err_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_widget_set_margin_start(err_box, 6);
        gtk_widget_set_margin_end(err_box, 6);
        gtk_widget_set_margin_top(err_box, 4);
        gtk_widget_set_margin_bottom(err_box, 4);
        gtk_widget_add_css_class(err_box, "error");

        ctx->error_label = gtk_label_new("");
        gtk_label_set_wrap(GTK_LABEL(ctx->error_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(ctx->error_label), 0.0f);
        gtk_box_append(GTK_BOX(err_box), ctx->error_label);

        gtk_revealer_set_child(GTK_REVEALER(ctx->error_revealer), err_box);
        gtk_box_append(GTK_BOX(main_box), ctx->error_revealer);
    }

    /* ---- Results row ---- */
    {
        GtkWidget *res_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

        GtkWidget *ssim_frame = make_result_frame("SSIM",
                                                   &ctx->ssim_min,
                                                   &ctx->ssim_max,
                                                   &ctx->ssim_mean);
        GtkWidget *ms_frame   = make_result_frame("MS-SSIM",
                                                   &ctx->ms_ssim_min,
                                                   &ctx->ms_ssim_max,
                                                   &ctx->ms_ssim_mean);
        ctx->vmaf_frame = make_result_frame("VMAF",
                                            &ctx->vmaf_min,
                                            &ctx->vmaf_max,
                                            &ctx->vmaf_mean);

        gtk_widget_set_hexpand(ssim_frame, TRUE);
        gtk_widget_set_hexpand(ms_frame,   TRUE);
        gtk_widget_set_hexpand(ctx->vmaf_frame, TRUE);

        gtk_box_append(GTK_BOX(res_box), ssim_frame);
        gtk_box_append(GTK_BOX(res_box), ms_frame);
        gtk_box_append(GTK_BOX(res_box), ctx->vmaf_frame);
        gtk_box_append(GTK_BOX(main_box), res_box);
    }
}

/* ================================================================== */
/*  Activate callback (called by GTK application)                     */
/* ================================================================== */

void ui_main_activate(GtkApplication *app, gpointer user_data)
{
    (void)user_data;

    ui_ctx *ctx = g_new0(ui_ctx, 1);
    app_state_init(&ctx->state);

    build_window(ctx, app);
    gtk_window_present(GTK_WINDOW(ctx->window));
}
