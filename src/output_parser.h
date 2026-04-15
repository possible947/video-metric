#ifndef OUTPUT_PARSER_H
#define OUTPUT_PARSER_H

#include <glib.h>

#include "app_state.h"

typedef enum parse_event_type {
    PARSE_EVENT_NONE = 0,
    PARSE_EVENT_STAGE_START,
    PARSE_EVENT_STAGE_DONE,
    PARSE_EVENT_RESULT_UPDATED,
    PARSE_EVENT_ERROR_LINE
} parse_event_type;

typedef enum metric_kind {
    METRIC_NONE = 0,
    METRIC_SSIM,
    METRIC_MS_SSIM,
    METRIC_VMAF
} metric_kind;

typedef struct parse_event {
    parse_event_type type;
    metric_kind metric;
    char *message;
} parse_event;

void parse_event_clear(parse_event *event);
gboolean output_parser_parse_line(app_state *state, const char *line, parse_event *event);

#endif

