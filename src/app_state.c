/*====================================================================*/
/*  FILE: src/app_state.c                                            */
/*====================================================================*/
#include "app_state.h"

#include <stdlib.h>
#include <string.h>

void app_state_init(app_state *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->resolution = strdup("hd");
    s->num_threads = 0;
}

void app_state_reset_results(app_state *s)
{
    if (!s) return;
    memset(&s->ssim,    0, sizeof(s->ssim));
    memset(&s->ms_ssim, 0, sizeof(s->ms_ssim));
    memset(&s->vmaf,    0, sizeof(s->vmaf));
}

void app_state_free(app_state *s)
{
    if (!s) return;
    free(s->orig_path);
    free(s->test_path);
    free(s->resolution);
    s->orig_path  = NULL;
    s->test_path  = NULL;
    s->resolution = NULL;
}

int app_state_count_metrics(const app_state *s)
{
    if (!s) return 0;
    return (s->use_ssim ? 1 : 0)
         + (s->use_ms_ssim ? 1 : 0)
         + (s->use_vmaf ? 1 : 0);
}
