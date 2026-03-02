/*====================================================================*/
/*  FILE: src/ms_ssim.c                                               */
/*====================================================================*/
/**
 * @file   ms_ssim.c
 * @brief  Compute MS-SSIM (Multi-Scale SSIM) between two videos.
 *
 * New implementation:
 *  - Decode videos through FFmpeg pipes (raw YUV)
 *  - Extract Y-plane for each frame
 *  - Compute MS-SSIM using pure C implementation
 *  - Display progress bar during computation
 *  - Return min/max/mean statistics
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <math.h>

#include "ms_ssim.h"
#include "pipe_decoder.h"
#include "msssim_core.h"

/* ------------------------------------------------------------------ */
/*  Helper: print progress bar                                        */
/* ------------------------------------------------------------------ */
static void print_progress_bar(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int filled = percent / 5;      /* 20 characters = 5% each */
    int empty = 20 - filled;

    printf("\r<%3d%%>", percent);
    for (int i = 0; i < filled; i++) printf("#");
    for (int i = 0; i < empty; i++) printf("-");
    printf(" ");
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*  Helper: compute statistics from values array                      */
/* ------------------------------------------------------------------ */
static void compute_statistics(const double *values, int count, metric_stats *stats)
{
    if (count == 0) {
        stats->min = 0.0;
        stats->max = 0.0;
        stats->mean = 0.0;
        return;
    }

    double sum = 0.0;
    double min_val = values[0];
    double max_val = values[0];

    for (int i = 0; i < count; i++) {
        sum += values[i];
        if (values[i] < min_val) min_val = values[i];
        if (values[i] > max_val) max_val = values[i];
    }

    stats->min = min_val;
    stats->max = max_val;
    stats->mean = sum / count;
}

/* ------------------------------------------------------------------ */
/*  Compute MS-SSIM between two videos                                */
/* ------------------------------------------------------------------ */
int compute_ms_ssim(const char *orig, const char *test, int threads, metric_stats *stats)
{
    /* Apply thread count (0 = auto-detect; positive = explicit) */
    msssim_set_num_threads(threads);

    if (!orig || !test || !stats) {
        fprintf(stderr, "compute_ms_ssim: NULL input path or stats\n");
        return -1;
    }

    /* Initialize stats */
    stats->min = INFINITY;
    stats->max = -INFINITY;
    stats->mean = NAN;

    /* Step 1: Get video metadata */
    VideoInfo ref_info, dist_info;

    if (get_video_info(orig, &ref_info) != 0) {
        fprintf(stderr, "compute_ms_ssim: failed to get metadata for reference video\n");
        return -1;
    }

    if (get_video_info(test, &dist_info) != 0) {
        fprintf(stderr, "compute_ms_ssim: failed to get metadata for test video\n");
        return -1;
    }

    /* Step 2: Validate compatibility */
    if (ref_info.width != dist_info.width || ref_info.height != dist_info.height) {
        fprintf(stderr, "compute_ms_ssim: resolution mismatch (%dx%d vs %dx%d)\n",
                ref_info.width, ref_info.height, dist_info.width, dist_info.height);
        return -1;
    }

    /* Check minimum dimensions for MS-SSIM (5-level pyramid needs at least 176x144) */
    if (ref_info.width < 176 || ref_info.height < 144) {
        fprintf(stderr, "compute_ms_ssim: resolution too small (%dx%d, minimum 176x144)\n",
                ref_info.width, ref_info.height);
        return -1;
    }

    int width = ref_info.width;
    int height = ref_info.height;
    int expected_frames = (ref_info.num_frames > 0) ? ref_info.num_frames : 1000;

    /* Step 3: Open both video pipes */
    PipeDecoder ref_dec, dist_dec;

    if (pipe_decoder_open(&ref_dec, orig, width, height) != 0) {
        fprintf(stderr, "compute_ms_ssim: failed to open reference video pipe\n");
        return -1;
    }

    if (pipe_decoder_open(&dist_dec, test, width, height) != 0) {
        fprintf(stderr, "compute_ms_ssim: failed to open test video pipe\n");
        pipe_decoder_close(&ref_dec);
        return -1;
    }

    /* Step 4: Allocate buffers */
    int plane_size = width * height;
    float *ref_y = malloc(plane_size * sizeof(float));
    float *dist_y = malloc(plane_size * sizeof(float));
    double *values = malloc(expected_frames * sizeof(double));

    if (!ref_y || !dist_y || !values) {
        fprintf(stderr, "compute_ms_ssim: memory allocation failed\n");
        free(ref_y);
        free(dist_y);
        free(values);
        pipe_decoder_close(&ref_dec);
        pipe_decoder_close(&dist_dec);
        return -1;
    }

    /* Step 5: Process frames */
    int frame_count = 0;
    int capacity = expected_frames;

    while (1) {
        /* Read Y-planes from both videos */
        int ref_status = pipe_decoder_read_y_plane(&ref_dec, ref_y);
        int dist_status = pipe_decoder_read_y_plane(&dist_dec, dist_y);

        /* Check for end of stream */
        if (ref_status != 0 || dist_status != 0) {
            if (ref_status != 0 && dist_status != 0) {
                /* Both ended simultaneously - normal */
                break;
            } else {
                /* One ended before the other - frame count mismatch */
                fprintf(stderr, "\nWarning: frame count mismatch between videos\n");
                break;
            }
        }

        /* Compute MS-SSIM for this frame */
        double msssim = compute_msssim_frame(ref_y, dist_y, width, height);

        if (msssim < 0.0) {
            fprintf(stderr, "\ncompute_ms_ssim: MS-SSIM computation failed on frame %d\n",
                    frame_count);
            break;
        }

        /* Store value (expand array if needed) */
        if (frame_count >= capacity) {
            capacity *= 2;
            double *new_values = realloc(values, capacity * sizeof(double));
            if (!new_values) {
                fprintf(stderr, "\ncompute_ms_ssim: memory reallocation failed\n");
                break;
            }
            values = new_values;
        }

        values[frame_count++] = msssim;

        /* Update progress bar */
        if (expected_frames > 0) {
            int percent = (frame_count * 100) / expected_frames;
            print_progress_bar(percent);
        }
    }

    printf("\r<%3d%%>", 99);
    for (int i = 0; i < 20; i++) printf("#");
    printf(" \n");
    fflush(stdout);

    /* Step 6: Compute statistics */
    if (frame_count > 0) {
        compute_statistics(values, frame_count, stats);
    } else {
        fprintf(stderr, "compute_ms_ssim: no frames processed\n");
        free(ref_y);
        free(dist_y);
        free(values);
        pipe_decoder_close(&ref_dec);
        pipe_decoder_close(&dist_dec);
        return -1;
    }

    /* Step 7: Cleanup */
    free(ref_y);
    free(dist_y);
    free(values);
    pipe_decoder_close(&ref_dec);
    pipe_decoder_close(&dist_dec);

    return 0;
}
