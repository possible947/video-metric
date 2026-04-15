#include "runner.h"

#include <gio/gio.h>
#include <string.h>

struct runner {
    GSubprocess *process;
    GDataInputStream *stdout_stream;
    GDataInputStream *stderr_stream;
    app_state *state;
    runner_event_cb on_event;
    runner_finished_cb on_finished;
    void *user_data;
    gboolean running;
};

static void runner_emit_line_event(runner *r, const char *line)
{
    parse_event ev = {0};
    gboolean parsed = FALSE;

    if (!r || !line || !r->on_event) {
        return;
    }

    if (r->state) {
        parsed = output_parser_parse_line(r->state, line, &ev);
    }

    if (parsed) {
        r->on_event(&ev, r->user_data);
        parse_event_clear(&ev);
        return;
    }

    if (g_str_has_prefix(line, "Error:") || g_str_has_prefix(line, "Warning:")) {
        ev.type = PARSE_EVENT_ERROR_LINE;
        ev.metric = METRIC_NONE;
        ev.message = g_strdup(line);
        r->on_event(&ev, r->user_data);
        parse_event_clear(&ev);
    }
}

static void read_stream_line_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    runner *r = (runner *)user_data;
    GDataInputStream *stream = G_DATA_INPUT_STREAM(source);
    gsize len = 0;
    GError *error = NULL;
    char *line;

    if (!r || !r->running) {
        return;
    }

    line = g_data_input_stream_read_line_finish_utf8(stream, res, &len, &error);
    if (error) {
        g_error_free(error);
        return;
    }

    if (!line) {
        return;
    }

    runner_emit_line_event(r, line);
    g_free(line);

    g_data_input_stream_read_line_async(stream,
                                        G_PRIORITY_DEFAULT,
                                        NULL,
                                        read_stream_line_cb,
                                        r);
}

static void process_wait_cb(GObject *source, GAsyncResult *res, gpointer user_data)
{
    runner *r = (runner *)user_data;
    GSubprocess *proc = G_SUBPROCESS(source);
    GError *error = NULL;
    gboolean ok;
    int status = 1;

    if (!r) {
        return;
    }

    ok = g_subprocess_wait_check_finish(proc, res, &error);
    r->running = FALSE;

    if (!ok) {
        status = 1;
        if (error) {
            parse_event ev = {0};
            ev.type = PARSE_EVENT_ERROR_LINE;
            ev.metric = METRIC_NONE;
            ev.message = g_strdup(error->message);
            if (r->on_event) {
                r->on_event(&ev, r->user_data);
            }
            parse_event_clear(&ev);
            g_error_free(error);
        }
    } else {
        status = 0;
    }

    if (r->on_finished) {
        r->on_finished(status, r->user_data);
    }
}

static GPtrArray *build_argv(const app_state *state)
{
    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);

    g_ptr_array_add(argv, g_strdup("./video_metric"));
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup(state->orig_path ? state->orig_path : ""));
    g_ptr_array_add(argv, g_strdup("-t"));
    g_ptr_array_add(argv, g_strdup(state->test_path ? state->test_path : ""));

    if (state->use_ssim) {
        g_ptr_array_add(argv, g_strdup("-s"));
    }
    if (state->use_ms_ssim) {
        g_ptr_array_add(argv, g_strdup("-m"));
    }
    if (state->use_vmaf) {
        g_ptr_array_add(argv, g_strdup("-v"));
        g_ptr_array_add(argv, g_strdup("-r"));
        g_ptr_array_add(argv, g_strdup(state->resolution ? state->resolution : "hd"));
    }

    if (state->num_threads >= 0) {
        g_ptr_array_add(argv, g_strdup("-n"));
        g_ptr_array_add(argv, g_strdup_printf("%d", state->num_threads));
    }

    g_ptr_array_add(argv, NULL);
    return argv;
}

runner *runner_new(void)
{
    return g_new0(runner, 1);
}

void runner_free(runner *r)
{
    if (!r) {
        return;
    }

    if (r->running) {
        runner_cancel(r);
    }

    g_clear_object(&r->stdout_stream);
    g_clear_object(&r->stderr_stream);
    g_clear_object(&r->process);
    g_free(r);
}

gboolean runner_start(runner *r,
                      app_state *state,
                      runner_event_cb on_event,
                      runner_finished_cb on_finished,
                      void *user_data,
                      char **error_message)
{
    GError *error = NULL;
    GPtrArray *argv;
    GInputStream *stdout_pipe;
    GInputStream *stderr_pipe;

    if (error_message) {
        *error_message = NULL;
    }

    if (!r || !state) {
        if (error_message) {
            *error_message = g_strdup("Internal error: runner state is invalid");
        }
        return FALSE;
    }

    if (r->running) {
        if (error_message) {
            *error_message = g_strdup("Runner is already active");
        }
        return FALSE;
    }

    r->on_event = on_event;
    r->on_finished = on_finished;
    r->user_data = user_data;
    r->state = state;

    argv = build_argv(state);
    r->process = g_subprocess_newv((const char * const *)argv->pdata,
                                   G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                   G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                   &error);
    g_ptr_array_free(argv, TRUE);

    if (!r->process) {
        if (error_message) {
            *error_message = g_strdup(error ? error->message : "Failed to launch process");
        }
        if (error) {
            g_error_free(error);
        }
        return FALSE;
    }

    stdout_pipe = g_subprocess_get_stdout_pipe(r->process);
    stderr_pipe = g_subprocess_get_stderr_pipe(r->process);

    r->stdout_stream = g_data_input_stream_new(stdout_pipe);
    r->stderr_stream = g_data_input_stream_new(stderr_pipe);

    r->running = TRUE;

    g_data_input_stream_read_line_async(r->stdout_stream,
                                        G_PRIORITY_DEFAULT,
                                        NULL,
                                        read_stream_line_cb,
                                        r);
    g_data_input_stream_read_line_async(r->stderr_stream,
                                        G_PRIORITY_DEFAULT,
                                        NULL,
                                        read_stream_line_cb,
                                        r);

    g_subprocess_wait_check_async(r->process, NULL, process_wait_cb, r);

    return TRUE;
}

void runner_cancel(runner *r)
{
    if (!r || !r->running || !r->process) {
        return;
    }

    g_subprocess_force_exit(r->process);
}

gboolean runner_is_running(const runner *r)
{
    return r ? r->running : FALSE;
}
