#include "output_parser.h"

#include <stdio.h>
#include <string.h>

static metric_kind detect_stage_start(const char *line)
{
    if (g_str_has_prefix(line, "Computing SSIM...")) {
        return METRIC_SSIM;
    }
    if (g_str_has_prefix(line, "Computing MS-SSIM...")) {
        return METRIC_MS_SSIM;
    }
    if (g_str_has_prefix(line, "Computing VMAF...")) {
        return METRIC_VMAF;
    }

    return METRIC_NONE;
}

static gboolean parse_metric_line(const char *line,
                                  const char *prefix,
                                  metric_result *target)
{
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;

    if (sscanf(line, "%*[^:]: %lf, Max: %lf, Mean: %lf", &min, &max, &mean) != 3) {
        return FALSE;
    }

    if (!g_str_has_prefix(line, prefix)) {
        return FALSE;
    }

    target->available = TRUE;
    target->min = min;
    target->max = max;
    target->mean = mean;
    return TRUE;
}

void parse_event_clear(parse_event *event)
{
    if (!event) {
        return;
    }

    g_clear_pointer(&event->message, g_free);
    event->type = PARSE_EVENT_NONE;
    event->metric = METRIC_NONE;
}

gboolean output_parser_parse_line(app_state *state, const char *line, parse_event *event)
{
    metric_kind metric;

    if (!state || !line || !event) {
        return FALSE;
    }

    parse_event_clear(event);

    metric = detect_stage_start(line);
    if (metric != METRIC_NONE) {
        event->type = PARSE_EVENT_STAGE_START;
        event->metric = metric;
        event->message = g_strdup(line);
        return TRUE;
    }

    if (parse_metric_line(line, "SSIM - Min:", &state->ssim)) {
        event->type = PARSE_EVENT_STAGE_DONE;
        event->metric = METRIC_SSIM;
        event->message = g_strdup(line);
        return TRUE;
    }

    if (parse_metric_line(line, "MS-SSIM - Min:", &state->ms_ssim)) {
        event->type = PARSE_EVENT_STAGE_DONE;
        event->metric = METRIC_MS_SSIM;
        event->message = g_strdup(line);
        return TRUE;
    }

    if (parse_metric_line(line, "VMAF - Min:", &state->vmaf)) {
        event->type = PARSE_EVENT_STAGE_DONE;
        event->metric = METRIC_VMAF;
        event->message = g_strdup(line);
        return TRUE;
    }

    if (g_str_has_prefix(line, "Error:") || g_str_has_prefix(line, "Warning:")) {
        event->type = PARSE_EVENT_ERROR_LINE;
        event->metric = METRIC_NONE;
        event->message = g_strdup(line);
        return TRUE;
    }

    return FALSE;
}
