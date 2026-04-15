#include "app_state.h"

#include <stdlib.h>
#include <string.h>

static void metric_result_reset(metric_result *res)
{
    if (!res) {
        return;
    }

    res->available = FALSE;
    res->min = 0.0;
    res->max = 0.0;
    res->mean = 0.0;
}

void app_state_init(app_state *state)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->resolution = g_strdup("hd");
    state->num_threads = 0;
    state->running = FALSE;

    app_state_reset_results(state);
}

void app_state_reset_results(app_state *state)
{
    if (!state) {
        return;
    }

    metric_result_reset(&state->ssim);
    metric_result_reset(&state->ms_ssim);
    metric_result_reset(&state->vmaf);
}

void app_state_clear_paths(app_state *state)
{
    if (!state) {
        return;
    }

    g_free(state->orig_path);
    g_free(state->test_path);
    state->orig_path = NULL;
    state->test_path = NULL;
}

void app_state_free(app_state *state)
{
    if (!state) {
        return;
    }

    app_state_clear_paths(state);
    g_free(state->resolution);
    state->resolution = NULL;
}

