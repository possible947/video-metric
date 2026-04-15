#include "ui_main.h"

#include "app_state.h"
#include "output_parser.h"
#include "runner.h"
#include "validation.h"

#include <stdio.h>

typedef struct ui_ctx {
    app_state state;
    runner *run;

    GtkWidget *window;
    GtkWidget *orig_button;
    GtkWidget *orig_label;
    GtkWidget *test_button;
    GtkWidget *test_label;
    GtkWidget *ssim_check;
    GtkWidget *ms_ssim_check;
    GtkWidget *vmaf_check;
    GtkWidget *resolution_combo;
    GtkWidget *threads_spin;
    GtkWidget *run_button;
    GtkWidget *cancel_button;
    GtkWidget *global_progress;
    GtkWidget *metric_progress;
    GtkWidget *status_label;
    GtkWidget *error_revealer;
    GtkWidget *error_label;

    GtkWidget *ssim_min;
    GtkWidget *ssim_max;
    GtkWidget *ssim_mean;
    GtkWidget *ms_min;
    GtkWidget *ms_max;
    GtkWidget *ms_mean;
    GtkWidget *vmaf_min;
    GtkWidget *vmaf_max;
    GtkWidget *vmaf_mean;

    int selected_metrics;
    int finished_metrics;
    metric_kind active_metric;
} ui_ctx;

static void ui_update_result_labels(ui_ctx *ctx)
{
    char buf[64];

    if (ctx->state.ssim.available) {
        g_snprintf(buf, sizeof(buf), "%.6f", ctx->state.ssim.min);
        gtk_label_set_text(GTK_LABEL(ctx->ssim_min), buf);
        g_snprintf(buf, sizeof(buf), "%.6f", ctx->state.ssim.max);
        gtk_label_set_text(GTK_LABEL(ctx->ssim_max), buf);
        g_snprintf(buf, sizeof(buf), "%.6f", ctx->state.ssim.mean);
        gtk_label_set_text(GTK_LABEL(ctx->ssim_mean), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(ctx->ssim_min), "-");
        gtk_label_set_text(GTK_LABEL(ctx->ssim_max), "-");
        gtk_label_set_text(GTK_LABEL(ctx->ssim_mean), "-");
    }

    if (ctx->state.ms_ssim.available) {
        g_snprintf(buf, sizeof(buf), "%.6f", ctx->state.ms_ssim.min);
        gtk_label_set_text(GTK_LABEL(ctx->ms_min), buf);
        g_snprintf(buf, sizeof(buf), "%.6f", ctx->state.ms_ssim.max);
        gtk_label_set_text(GTK_LABEL(ctx->ms_max), buf);
        g_snprintf(buf, sizeof(buf), "%.6f", ctx->state.ms_ssim.mean);
        gtk_label_set_text(GTK_LABEL(ctx->ms_mean), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(ctx->ms_min), "-");
        gtk_label_set_text(GTK_LABEL(ctx->ms_max), "-");
        gtk_label_set_text(GTK_LABEL(ctx->ms_mean), "-");
    }

    if (ctx->state.vmaf.available) {
        g_snprintf(buf, sizeof(buf), "%.6f", ctx->state.vmaf.min);
        gtk_label_set_text(GTK_LABEL(ctx->vmaf_min), buf);
        g_snprintf(buf, sizeof(buf), "%.6f", ctx->state.vmaf.max);
        gtk_label_set_text(GTK_LABEL(ctx->vmaf_max), buf);
        g_snprintf(buf, sizeof(buf), "%.6f", ctx->state.vmaf.mean);
        gtk_label_set_text(GTK_LABEL(ctx->vmaf_mean), buf);
    } else {
        gtk_label_set_text(GTK_LABEL(ctx->vmaf_min), "-");
        gtk_label_set_text(GTK_LABEL(ctx->vmaf_max), "-");
        gtk_label_set_text(GTK_LABEL(ctx->vmaf_mean), "-");
    }
}

static void ui_set_error(ui_ctx *ctx, const char *message)
{
    if (message && *message) {
        gtk_label_set_text(GTK_LABEL(ctx->error_label), message);
        gtk_revealer_set_reveal_child(GTK_REVEALER(ctx->error_revealer), TRUE);
    } else {
        gtk_revealer_set_reveal_child(GTK_REVEALER(ctx->error_revealer), FALSE);
        gtk_label_set_text(GTK_LABEL(ctx->error_label), "");
    }
}

static const char *metric_to_name(metric_kind k)
{
    switch (k) {
    case METRIC_SSIM: return "SSIM";
    case METRIC_MS_SSIM: return "MS-SSIM";
    case METRIC_VMAF: return "VMAF";
    default: return "-";
    }
}

static int count_selected_metrics(const app_state *state)
{
    int c = 0;
    if (state->use_ssim) c++;
    if (state->use_ms_ssim) c++;
    if (state->use_vmaf) c++;
    return c;
}

static void ui_refresh_running_state(ui_ctx *ctx)
{
    gboolean running = ctx->state.running;
    gtk_widget_set_sensitive(ctx->orig_button, !running);
    gtk_widget_set_sensitive(ctx->test_button, !running);
    gtk_widget_set_sensitive(ctx->ssim_check, !running);
    gtk_widget_set_sensitive(ctx->ms_ssim_check, !running);
    gtk_widget_set_sensitive(ctx->vmaf_check, !running);
    gtk_widget_set_sensitive(ctx->resolution_combo, !running && ctx->state.use_vmaf);
    gtk_widget_set_sensitive(ctx->threads_spin, !running);
    gtk_widget_set_sensitive(ctx->run_button, !running);
    gtk_widget_set_sensitive(ctx->cancel_button, running);
}

static void on_event_cb(const parse_event *event, void *user_data)
{
    ui_ctx *ctx = (ui_ctx *)user_data;
    char status[128];

    if (!ctx || !event) {
        return;
    }

    switch (event->type) {
    case PARSE_EVENT_STAGE_START:
        ctx->active_metric = event->metric;
        g_snprintf(status, sizeof(status), "Running: %s", metric_to_name(event->metric));
        gtk_label_set_text(GTK_LABEL(ctx->status_label), status);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->metric_progress), 0.2);
        break;
    case PARSE_EVENT_STAGE_DONE:
        ctx->finished_metrics++;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->metric_progress), 1.0);
        if (ctx->selected_metrics > 0) {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->global_progress),
                                          (double)ctx->finished_metrics / (double)ctx->selected_metrics);
        }
        ui_update_result_labels(ctx);
        break;
    case PARSE_EVENT_ERROR_LINE:
        if (event->message) {
            ui_set_error(ctx, event->message);
        }
        break;
    default:
        break;
    }
}

static void on_finished_cb(int exit_status, void *user_data)
{
    ui_ctx *ctx = (ui_ctx *)user_data;
    if (!ctx) {
        return;
    }

    ctx->state.running = FALSE;
    if (exit_status == 0) {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Completed");
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->metric_progress), 1.0);
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->global_progress), 1.0);
    } else {
        gtk_label_set_text(GTK_LABEL(ctx->status_label), "Failed");
        if (!gtk_revealer_get_reveal_child(GTK_REVEALER(ctx->error_revealer))) {
            ui_set_error(ctx, "Computation failed");
        }
    }

    ui_refresh_running_state(ctx);
}

static void on_toggle_metric(GtkCheckButton *button, gpointer user_data)
{
    ui_ctx *ctx = (ui_ctx *)user_data;
    (void)button;
    ctx->state.use_ssim = gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->ssim_check));
    ctx->state.use_ms_ssim = gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->ms_ssim_check));
    ctx->state.use_vmaf = gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->vmaf_check));
    gtk_widget_set_sensitive(ctx->resolution_combo, !ctx->state.running && ctx->state.use_vmaf);
}

static void on_resolution_changed(GtkComboBoxText *combo, gpointer user_data)
{
    ui_ctx *ctx = (ui_ctx *)user_data;
    const char *text = gtk_combo_box_text_get_active_text(combo);
    if (!text) {
        return;
    }
    g_free(ctx->state.resolution);
    ctx->state.resolution = g_strdup(text);
}

static void on_threads_changed(GtkSpinButton *spin, gpointer user_data)
{
    ui_ctx *ctx = (ui_ctx *)user_data;
    ctx->state.num_threads = gtk_spin_button_get_value_as_int(spin);
}

static void file_dialog_done(GtkFileDialog *dialog, GAsyncResult *res, gpointer user_data)
{
    gpointer *arr = (gpointer *)user_data;
    ui_ctx *ctx = (ui_ctx *)arr[0];
    gboolean is_orig = GPOINTER_TO_INT(arr[1]);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(dialog, res, &error);

    if (error) {
        g_error_free(error);
        g_free(arr);
        return;
    }

    if (file) {
        char *path = g_file_get_path(file);
        if (is_orig) {
            g_free(ctx->state.orig_path);
            ctx->state.orig_path = g_strdup(path);
            gtk_label_set_text(GTK_LABEL(ctx->orig_label), path);
        } else {
            g_free(ctx->state.test_path);
            ctx->state.test_path = g_strdup(path);
            gtk_label_set_text(GTK_LABEL(ctx->test_label), path);
        }
        g_free(path);
        g_object_unref(file);
    }

    g_free(arr);
}

static void open_file_dialog(ui_ctx *ctx, gboolean is_orig)
{
    GtkFileDialog *dialog = gtk_file_dialog_new();
    gpointer *arr = g_new0(gpointer, 2);
    arr[0] = ctx;
    arr[1] = GINT_TO_POINTER(is_orig ? 1 : 0);

    gtk_file_dialog_open(dialog,
                         GTK_WINDOW(ctx->window),
                         NULL,
                         file_dialog_done,
                         arr);
    g_object_unref(dialog);
}

static void on_pick_orig(GtkButton *button, gpointer user_data)
{
    (void)button;
    open_file_dialog((ui_ctx *)user_data, TRUE);
}

static void on_pick_test(GtkButton *button, gpointer user_data)
{
    (void)button;
    open_file_dialog((ui_ctx *)user_data, FALSE);
}

static void on_cancel(GtkButton *button, gpointer user_data)
{
    ui_ctx *ctx = (ui_ctx *)user_data;
    (void)button;

    if (!ctx->state.running) {
        return;
    }

    runner_cancel(ctx->run);
    gtk_label_set_text(GTK_LABEL(ctx->status_label), "Cancelling...");
}

static void on_run(GtkButton *button, gpointer user_data)
{
    ui_ctx *ctx = (ui_ctx *)user_data;
    char *error_message = NULL;
    gboolean ok;

    (void)button;

    ui_set_error(ctx, NULL);
    ctx->state.use_ssim = gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->ssim_check));
    ctx->state.use_ms_ssim = gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->ms_ssim_check));
    ctx->state.use_vmaf = gtk_check_button_get_active(GTK_CHECK_BUTTON(ctx->vmaf_check));
    ctx->state.num_threads = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ctx->threads_spin));

    if (!validate_before_run(&ctx->state, &error_message)) {
        ui_set_error(ctx, error_message ? error_message : "Validation failed");
        g_free(error_message);
        return;
    }

    app_state_reset_results(&ctx->state);
    ui_update_result_labels(ctx);
    ctx->selected_metrics = count_selected_metrics(&ctx->state);
    ctx->finished_metrics = 0;
    ctx->active_metric = METRIC_NONE;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->global_progress), 0.0);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ctx->metric_progress), 0.0);
    gtk_label_set_text(GTK_LABEL(ctx->status_label), "Starting...");

    ctx->state.running = TRUE;
    ui_refresh_running_state(ctx);

    ok = runner_start(ctx->run,
                      &ctx->state,
                      on_event_cb,
                      on_finished_cb,
                      ctx,
                      &error_message);
    if (!ok) {
        ctx->state.running = FALSE;
        ui_refresh_running_state(ctx);
        ui_set_error(ctx, error_message ? error_message : "Could not start process");
        g_free(error_message);
        return;
    }
}

static GtkWidget *create_result_frame(const char *title,
                                      GtkWidget **min_label,
                                      GtkWidget **max_label,
                                      GtkWidget **mean_label)
{
    GtkWidget *frame = gtk_frame_new(title);
    GtkWidget *grid = gtk_grid_new();

    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_widget_set_margin_top(grid, 8);
    gtk_widget_set_margin_bottom(grid, 8);
    gtk_widget_set_margin_start(grid, 8);
    gtk_widget_set_margin_end(grid, 8);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Min:"), 0, 0, 1, 1);
    *min_label = gtk_label_new("-");
    gtk_grid_attach(GTK_GRID(grid), *min_label, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Max:"), 0, 1, 1, 1);
    *max_label = gtk_label_new("-");
    gtk_grid_attach(GTK_GRID(grid), *max_label, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Mean:"), 0, 2, 1, 1);
    *mean_label = gtk_label_new("-");
    gtk_grid_attach(GTK_GRID(grid), *mean_label, 1, 2, 1, 1);

    gtk_frame_set_child(GTK_FRAME(frame), grid);
    return frame;
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data)
{
    ui_ctx *ctx = (ui_ctx *)user_data;
    (void)widget;

    if (!ctx) {
        return;
    }

    runner_free(ctx->run);
    app_state_free(&ctx->state);
    g_free(ctx);
}

void ui_main_activate(GtkApplication *app, gpointer user_data)
{
    ui_ctx *ctx = g_new0(ui_ctx, 1);
    GtkWidget *root;
    GtkWidget *header;
    GtkWidget *inputs_grid;
    GtkWidget *options_box;
    GtkWidget *actions_box;
    GtkWidget *progress_box;
    GtkWidget *results_box;

    (void)user_data;

    app_state_init(&ctx->state);
    ctx->run = runner_new();

    ctx->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(ctx->window), "video_metric GUI");
    gtk_window_set_default_size(GTK_WINDOW(ctx->window), 0, 0);

    root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 12);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    gtk_window_set_child(GTK_WINDOW(ctx->window), root);

    header = gtk_label_new("Video Metric – GTK4 GUI");
    gtk_widget_add_css_class(header, "title-2");
    gtk_box_append(GTK_BOX(root), header);

    inputs_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(inputs_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(inputs_grid), 8);
    gtk_box_append(GTK_BOX(root), inputs_grid);

    gtk_grid_attach(GTK_GRID(inputs_grid), gtk_label_new("Original:"), 0, 0, 1, 1);
    ctx->orig_button = gtk_button_new_with_label("Select original video");
    g_signal_connect(ctx->orig_button, "clicked", G_CALLBACK(on_pick_orig), ctx);
    gtk_grid_attach(GTK_GRID(inputs_grid), ctx->orig_button, 1, 0, 1, 1);
    ctx->orig_label = gtk_label_new("-");
    gtk_label_set_xalign(GTK_LABEL(ctx->orig_label), 0.0f);
    gtk_grid_attach(GTK_GRID(inputs_grid), ctx->orig_label, 2, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(inputs_grid), gtk_label_new("Test:"), 0, 1, 1, 1);
    ctx->test_button = gtk_button_new_with_label("Select test video");
    g_signal_connect(ctx->test_button, "clicked", G_CALLBACK(on_pick_test), ctx);
    gtk_grid_attach(GTK_GRID(inputs_grid), ctx->test_button, 1, 1, 1, 1);
    ctx->test_label = gtk_label_new("-");
    gtk_label_set_xalign(GTK_LABEL(ctx->test_label), 0.0f);
    gtk_grid_attach(GTK_GRID(inputs_grid), ctx->test_label, 2, 1, 1, 1);

    options_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(root), options_box);

    ctx->ssim_check = gtk_check_button_new_with_label("SSIM");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ctx->ssim_check), TRUE);
    ctx->ms_ssim_check = gtk_check_button_new_with_label("MS-SSIM");
    ctx->vmaf_check = gtk_check_button_new_with_label("VMAF");
    gtk_box_append(GTK_BOX(options_box), ctx->ssim_check);
    gtk_box_append(GTK_BOX(options_box), ctx->ms_ssim_check);
    gtk_box_append(GTK_BOX(options_box), ctx->vmaf_check);
    g_signal_connect(ctx->ssim_check, "toggled", G_CALLBACK(on_toggle_metric), ctx);
    g_signal_connect(ctx->ms_ssim_check, "toggled", G_CALLBACK(on_toggle_metric), ctx);
    g_signal_connect(ctx->vmaf_check, "toggled", G_CALLBACK(on_toggle_metric), ctx);

    gtk_box_append(GTK_BOX(options_box), gtk_label_new("Resolution:"));
    ctx->resolution_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ctx->resolution_combo), "hd");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ctx->resolution_combo), "4k");
    gtk_combo_box_set_active(GTK_COMBO_BOX(ctx->resolution_combo), 0);
    g_signal_connect(ctx->resolution_combo, "changed", G_CALLBACK(on_resolution_changed), ctx);
    gtk_box_append(GTK_BOX(options_box), ctx->resolution_combo);

    gtk_box_append(GTK_BOX(options_box), gtk_label_new("Threads:"));
    ctx->threads_spin = gtk_spin_button_new_with_range(0, 1024, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ctx->threads_spin), 0);
    g_signal_connect(ctx->threads_spin, "value-changed", G_CALLBACK(on_threads_changed), ctx);
    gtk_box_append(GTK_BOX(options_box), ctx->threads_spin);

    actions_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(root), actions_box);
    ctx->run_button = gtk_button_new_with_label("Run");
    ctx->cancel_button = gtk_button_new_with_label("Cancel");
    gtk_widget_set_sensitive(ctx->cancel_button, FALSE);
    gtk_box_append(GTK_BOX(actions_box), ctx->run_button);
    gtk_box_append(GTK_BOX(actions_box), ctx->cancel_button);
    g_signal_connect(ctx->run_button, "clicked", G_CALLBACK(on_run), ctx);
    g_signal_connect(ctx->cancel_button, "clicked", G_CALLBACK(on_cancel), ctx);

    progress_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_append(GTK_BOX(root), progress_box);
    ctx->status_label = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(ctx->status_label), 0.0f);
    gtk_box_append(GTK_BOX(progress_box), ctx->status_label);
    ctx->global_progress = gtk_progress_bar_new();
    ctx->metric_progress = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(progress_box), ctx->global_progress);
    gtk_box_append(GTK_BOX(progress_box), ctx->metric_progress);

    ctx->error_revealer = gtk_revealer_new();
    gtk_revealer_set_reveal_child(GTK_REVEALER(ctx->error_revealer), FALSE);
    ctx->error_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(ctx->error_label), 0.0f);
    gtk_revealer_set_child(GTK_REVEALER(ctx->error_revealer), ctx->error_label);
    gtk_box_append(GTK_BOX(root), ctx->error_revealer);

    results_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_append(GTK_BOX(root), results_box);
    gtk_box_append(GTK_BOX(results_box), create_result_frame("SSIM", &ctx->ssim_min, &ctx->ssim_max, &ctx->ssim_mean));
    gtk_box_append(GTK_BOX(results_box), create_result_frame("MS-SSIM", &ctx->ms_min, &ctx->ms_max, &ctx->ms_mean));
    gtk_box_append(GTK_BOX(results_box), create_result_frame("VMAF", &ctx->vmaf_min, &ctx->vmaf_max, &ctx->vmaf_mean));

    ui_update_result_labels(ctx);
    ui_refresh_running_state(ctx);

    g_signal_connect(ctx->window, "destroy", G_CALLBACK(on_window_destroy), ctx);
    gtk_widget_show(ctx->window);
}
