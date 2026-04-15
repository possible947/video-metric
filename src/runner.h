#ifndef RUNNER_H
#define RUNNER_H

#include <glib.h>

#include "app_state.h"
#include "output_parser.h"

typedef struct runner runner;

typedef void (*runner_event_cb)(const parse_event *event, void *user_data);
typedef void (*runner_finished_cb)(int exit_status, void *user_data);

runner *runner_new(void);
void runner_free(runner *r);

gboolean runner_start(runner *r,
                      app_state *state,
                      runner_event_cb on_event,
                      runner_finished_cb on_finished,
                      void *user_data,
                      char **error_message);

void runner_cancel(runner *r);
gboolean runner_is_running(const runner *r);

#endif
