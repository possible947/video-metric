#include "validation.h"

#include <gio/gio.h>
#include <string.h>

static gboolean file_exists_regular(const char *path)
{
    GFile *file;
    GFileType type;
    gboolean ok = FALSE;

    if (!path || !*path) {
        return FALSE;
    }

    file = g_file_new_for_path(path);
    type = g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, NULL);
    ok = (type == G_FILE_TYPE_REGULAR);
    g_object_unref(file);
    return ok;
}

gboolean validate_before_run(const app_state *state, char **error_message)
{
    if (error_message) {
        *error_message = NULL;
    }

    if (!state) {
        if (error_message) {
            *error_message = g_strdup("Internal error: missing state");
        }
        return FALSE;
    }

    if (!file_exists_regular(state->orig_path)) {
        if (error_message) {
            *error_message = g_strdup("Original video file is missing or unreadable");
        }
        return FALSE;
    }

    if (!file_exists_regular(state->test_path)) {
        if (error_message) {
            *error_message = g_strdup("Test video file is missing or unreadable");
        }
        return FALSE;
    }

    if (!state->use_ssim && !state->use_ms_ssim && !state->use_vmaf) {
        if (error_message) {
            *error_message = g_strdup("Select at least one metric");
        }
        return FALSE;
    }

    if (state->num_threads < 0) {
        if (error_message) {
            *error_message = g_strdup("Threads must be a non-negative integer");
        }
        return FALSE;
    }

    if (state->use_vmaf) {
        if (!state->resolution ||
            (strcmp(state->resolution, "hd") != 0 && strcmp(state->resolution, "4k") != 0)) {
            if (error_message) {
                *error_message = g_strdup("VMAF resolution must be hd or 4k");
            }
            return FALSE;
        }
    }

    return TRUE;
}

