#ifndef VALIDATION_H
#define VALIDATION_H

#include <glib.h>

#include "app_state.h"

gboolean validate_before_run(const app_state *state, char **error_message);

#endif

